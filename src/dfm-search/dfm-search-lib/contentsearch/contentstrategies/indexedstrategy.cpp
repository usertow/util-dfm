// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "indexedstrategy.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QTextStream>
#include <QThread>
#include <QElapsedTimer>

#include "utils/contenthighlighter.h"
#include "utils/searchutility.h"

DFM_SEARCH_BEGIN_NS

ContentIndexedStrategy::ContentIndexedStrategy(const SearchOptions &options, QObject *parent)
    : ContentBaseStrategy(options, parent)
{
    initializeIndexing();
}

ContentIndexedStrategy::~ContentIndexedStrategy() = default;

void ContentIndexedStrategy::initializeIndexing()
{
    // 获取索引目录
    m_indexDir = Global::contentIndexDirectory();

    // 检查索引目录是否存在
    if (!QDir(m_indexDir).exists()) {
        qWarning() << "Content index directory does not exist:" << m_indexDir;
    }
}

void ContentIndexedStrategy::search(const SearchQuery &query)
{
    m_cancelled.store(false);
    m_results.clear();

    try {
        // 执行内容索引搜索
        performContentSearch(query);
    } catch (const std::exception &e) {
        qWarning() << "Content Index Search Exception:" << e.what();
        emit errorOccurred(SearchError(ContentSearchErrorCode::ContentIndexException));
    }
}

Xapian::Query ContentIndexedStrategy::buildXapianQuery(const SearchQuery &query, const QString &searchPath)
{
    try {
        m_keywords.clear();
        ContentOptionsAPI optAPI(m_options);   // Use the member m_options
        bool mixedAndEnabled = optAPI.isFilenameContentMixedAndSearchEnabled();

        Xapian::QueryParser contentsParser;
        // Assume 'contents' is the default field (no prefix)
        // If it has a prefix, use contentsParser.add_prefix("contents", "C");

        Xapian::Query mainQuery;
        if (query.type() == SearchQuery::Type::Simple) {
            mainQuery = buildSimpleContentsQuery(query, contentsParser);
        } else if (query.type() == SearchQuery::Type::Boolean) {
            if (query.subQueries().isEmpty()) {
                mainQuery = Xapian::Query();
            } else {
                if (mixedAndEnabled && query.booleanOperator() == SearchQuery::BooleanOperator::AND) {
                    mainQuery = buildAdvancedAndQuery(query, contentsParser);
                } else {
                    mainQuery = buildStandardBooleanContentsQuery(query, contentsParser);
                }
            }
        } else {
            qWarning() << "Unknown SearchQuery type encountered.";
            mainQuery = Xapian::Query();
        }

        // Add path prefix query optimization
        if (!mainQuery.empty() && SearchUtility::shouldUsePathPrefixQuery(searchPath)) {
            // Assume ancestor paths are indexed with prefix 'P'
            Xapian::Query pathPrefixQuery("P" + searchPath.toStdString());
            mainQuery = Xapian::Query(Xapian::Query::OP_AND, mainQuery, pathPrefixQuery);
            qInfo() << "Using path prefix query for content search optimization:" << searchPath;
        }

        return mainQuery;

    } catch (const Xapian::Error &e) {
        qWarning() << "Error building Xapian query:" << QString::fromStdString(e.get_msg());
        return Xapian::Query();
    } catch (const std::exception &e) {
        qWarning() << "Standard exception building Xapian query:" << e.what();
        return Xapian::Query();
    }
}

Xapian::Query ContentIndexedStrategy::buildAdvancedAndQuery(const SearchQuery &query, Xapian::QueryParser &contentsParser)
{
    Xapian::QueryParser filenameParser;
    filenameParser.add_prefix("filename", "F");

    std::vector<Xapian::Query> andClauses;
    std::vector<Xapian::Query> allContentsClauses;
    std::vector<Xapian::Query> allFilenamesClauses;
    bool hasValidKeywords = false;

    for (const auto &subQuery : query.subQueries()) {
        m_keywords.append(subQuery.keyword());
        if (subQuery.keyword().isEmpty()) {
            continue;
        }
        hasValidKeywords = true;

        std::string keyword = subQuery.keyword().toStdString();
        Xapian::Query contentsTermQuery = contentsParser.parse_query(keyword);
        Xapian::Query filenameTermQuery = filenameParser.parse_query("filename:" + keyword);

        Xapian::Query combinedTermQuery(Xapian::Query::OP_OR, contentsTermQuery, filenameTermQuery);

        andClauses.push_back(combinedTermQuery);
        allContentsClauses.push_back(contentsTermQuery);
        allFilenamesClauses.push_back(filenameTermQuery);
    }

    if (!hasValidKeywords) {
        return Xapian::Query();
    }

    Xapian::Query mainAndClausesQuery(Xapian::Query::OP_AND, andClauses.begin(), andClauses.end());
    Xapian::Query allContentsQuery(Xapian::Query::OP_AND, allContentsClauses.begin(), allContentsClauses.end());
    Xapian::Query allFilenamesQuery(Xapian::Query::OP_AND, allFilenamesClauses.begin(), allFilenamesClauses.end());

    // Final query: mainAndClausesQuery AND NOT (allFilenamesQuery AND NOT allContentsQuery)
    Xapian::Query pureFilenameQuery(Xapian::Query::OP_AND_NOT, allFilenamesQuery, allContentsQuery);
    
    return Xapian::Query(Xapian::Query::OP_AND_NOT, mainAndClausesQuery, pureFilenameQuery);
}

Xapian::Query ContentIndexedStrategy::buildStandardBooleanContentsQuery(const SearchQuery &query, Xapian::QueryParser &contentsParser)
{
    std::vector<Xapian::Query> subQueries;

    for (const auto &subQuery : query.subQueries()) {
        m_keywords.append(subQuery.keyword());
        if (subQuery.keyword().isEmpty()) {
            continue;
        }

        Xapian::Query termQuery = contentsParser.parse_query(subQuery.keyword().toStdString());
        subQueries.push_back(termQuery);
    }

    if (subQueries.empty()) return Xapian::Query();

    return Xapian::Query(query.booleanOperator() == SearchQuery::BooleanOperator::AND ? Xapian::Query::OP_AND : Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

Xapian::Query ContentIndexedStrategy::buildSimpleContentsQuery(const SearchQuery &query, Xapian::QueryParser &contentsParser)
{
    m_keywords.append(query.keyword());
    return contentsParser.parse_query(query.keyword().toStdString());
}
void ContentIndexedStrategy::processSearchResults(const Xapian::MSet &mset)
{
    // Measure the time taken to process search results
    QElapsedTimer resultTimer;
    resultTimer.start();

    const QString searchPath = m_options.searchPath();
    const QStringList searchExcludedPaths = m_options.searchExcludedPaths();
    ContentOptionsAPI optAPI(m_options);
    const bool enableRetrieval = optAPI.isFullTextRetrievalEnabled();
    const int previewLen = optAPI.maxPreviewLength() > 0 ? optAPI.maxPreviewLength() : 50;
    const bool enableHTML = optAPI.isSearchResultHighlightEnabled();

    m_results.reserve(mset.size());

    // 实时处理搜索结果
    for (Xapian::MSetIterator it = mset.begin(); it != mset.end(); ++it) {
        if (m_cancelled.load()) {
            qInfo() << "Content search cancelled";
            break;
        }

        try {
            Xapian::Document doc = it.get_document();
            // Assume path is stored in the document data or a value
            QString path = QString::fromStdString(doc.get_data());

            if (!path.startsWith(searchPath)) {
                continue;
            }

            if (std::any_of(searchExcludedPaths.cbegin(), searchExcludedPaths.cend(),
                            [&path](const auto &excluded) { return path.startsWith(excluded); })) {
                continue;
            }

            // Create search result
            SearchResult result(path);
            ContentResultAPI resultApi(result);

            if (enableRetrieval) {
                // Assume content is stored in a value or a specific field in data
                // For now, let's assume it's in a value slot (SLOT_CONTENT)
                std::string contentStr = doc.get_value(1); // SLOT_CONTENT
                if (!contentStr.empty()) {
                    const QString content = QString::fromStdString(contentStr);
                    const QString highlightedContent = ContentHighlighter::customHighlight(m_keywords, content, previewLen, enableHTML);
                    resultApi.setHighlightedContent(highlightedContent);
                }
            }

            m_results.append(result);

            if (Q_UNLIKELY(m_options.resultFoundEnabled()))
                emit resultFound(result);

        } catch (const Xapian::Error &e) {
            qWarning() << "Error processing result:" << QString::fromStdString(e.get_msg());
            continue;
        } catch (const std::exception &e) {
            qWarning() << "Standard exception:" << e.what();
            continue;
        }
    }

    qInfo() << "Content result processing time:" << resultTimer.elapsed() << "ms";
    emit searchFinished(m_results);
}

void ContentIndexedStrategy::performContentSearch(const SearchQuery &query)
{
    BaseSearchStrategy::SearchCancellationGuard guard(&m_cancelled);

    try {
        // Open Xapian database
        Xapian::Database db;
        try {
            db = Xapian::Database(m_indexDir.toStdString());
        } catch (const Xapian::DatabaseOpeningError &e) {
            qWarning() << "Failed to open Xapian database:" << m_indexDir;
            emit errorOccurred(SearchError(ContentSearchErrorCode::ContentIndexNotFound));
            return;
        }

        // Build Xapian query
        m_currentQuery = buildXapianQuery(query, m_options.searchPath());
        if (m_currentQuery.empty()) {
            qWarning() << "Failed to build Xapian query";
            emit errorOccurred(SearchError(ContentSearchErrorCode::ContentIndexException));
            return;
        }

        // Execute search
        QElapsedTimer searchTimer;
        searchTimer.start();

        Xapian::Enquire enquire(db);
        enquire.set_query(m_currentQuery);

        int32_t maxResults = m_options.maxResults() > 0 ? m_options.maxResults() : 10000;
        Xapian::MSet mset = enquire.get_mset(0, maxResults);

        qInfo() << "Content search execution time:" << searchTimer.elapsed() << "ms"
                << "Total hits:" << mset.get_matches_estimated()
                << "Collected:" << mset.size()
                << "Keyword:" << query.keyword()
                << "Cancelled" << m_cancelled.load();

        // Process search results
        processSearchResults(mset);
    } catch (const Xapian::Error &e) {
        qWarning() << "Xapian search exception:" << QString::fromStdString(e.get_msg());
        emit errorOccurred(SearchError(ContentSearchErrorCode::ContentIndexException));
    } catch (const std::exception &e) {
        qWarning() << "Standard exception:" << e.what();
        emit errorOccurred(SearchError(ContentSearchErrorCode::ContentIndexException));
    }
}

void ContentIndexedStrategy::cancel()
{
    m_cancelled.store(true);
}

DFM_SEARCH_END_NS
