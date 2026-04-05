// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "indexedstrategy.h"
#include "utils/searchutility.h"

#include <unistd.h>
#include <sys/types.h>

#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QElapsedTimer>

DFM_SEARCH_BEGIN_NS

//--------------------------------------------------------------------
// QueryBuilder 实现
//--------------------------------------------------------------------

QueryBuilder::QueryBuilder()
{
}

Xapian::Query QueryBuilder::buildTypeQuery(const QStringList &types) const
{
    if (types.isEmpty()) {
        return Xapian::Query();
    }

    std::vector<Xapian::Query> subQueries;
    for (const QString &type : types) {
        QString cleanType = type.trimmed().toLower();
        if (!cleanType.isEmpty()) {
            subQueries.push_back(Xapian::Query("T" + cleanType.toStdString()));
        }
    }

    if (subQueries.empty()) {
        return Xapian::Query();
    }

    return Xapian::Query(Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

Xapian::Query QueryBuilder::buildExtQuery(const QStringList &extensions) const
{
    if (extensions.isEmpty()) {
        return Xapian::Query();
    }

    std::vector<Xapian::Query> subQueries;
    for (const QString &ext : extensions) {
        QString cleanExt = ext.trimmed().toLower();
        if (!cleanExt.isEmpty()) {
            subQueries.push_back(Xapian::Query("E" + cleanExt.toStdString()));
        }
    }

    if (subQueries.empty()) {
        return Xapian::Query();
    }

    return Xapian::Query(Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

Xapian::Query QueryBuilder::buildPinyinQuery(const QStringList &pinyins, SearchQuery::BooleanOperator op) const
{
    if (pinyins.isEmpty()) {
        return Xapian::Query();
    }

    std::vector<Xapian::Query> subQueries;
    for (const QString &pinyin : pinyins) {
        QString cleanPinyin = pinyin.trimmed();
        if (!cleanPinyin.isEmpty() && Global::isPinyinSequence(cleanPinyin)) {
            subQueries.push_back(Xapian::Query("P" + cleanPinyin.toStdString()));
        }
    }

    if (subQueries.empty()) {
        return Xapian::Query();
    }

    return Xapian::Query(op == SearchQuery::BooleanOperator::AND ? Xapian::Query::OP_AND : Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

Xapian::Query QueryBuilder::buildPinyinAcronymQuery(const QStringList &acronyms, SearchQuery::BooleanOperator op) const
{
    if (acronyms.isEmpty()) {
        return Xapian::Query();
    }

    std::vector<Xapian::Query> subQueries;
    for (const QString &acronym : acronyms) {
        QString cleanAcronym = acronym.trimmed();
        if (!cleanAcronym.isEmpty()) {
            subQueries.push_back(Xapian::Query("A" + cleanAcronym.toStdString()));
        }
    }

    if (subQueries.empty()) {
        return Xapian::Query();
    }

    return Xapian::Query(op == SearchQuery::BooleanOperator::AND ? Xapian::Query::OP_AND : Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

Xapian::Query QueryBuilder::buildCommonQuery(const QString &keyword, bool caseSensitive, bool allowWildcard) const
{
    if (keyword.isEmpty()) {
        return Xapian::Query();
    }

    Xapian::QueryParser parser;
    int flags = Xapian::QueryParser::FLAG_DEFAULT;
    if (allowWildcard) {
        flags |= Xapian::QueryParser::FLAG_WILDCARD;
    }

    std::string processedKeyword = caseSensitive ? keyword.toStdString() : keyword.toLower().toStdString();
    return parser.parse_query(processedKeyword, flags);
}

Xapian::Query QueryBuilder::buildCommonQuery(const QString &keyword, bool caseSensitive, const QString &fieldName, bool allowWildcard) const
{
    if (keyword.isEmpty() || fieldName.isEmpty()) {
        return Xapian::Query();
    }

    // Map field name to prefix
    std::string prefix;
    if (fieldName == "file_name") prefix = "";   // Default field
    else if (fieldName == "file_type") prefix = "T";
    else if (fieldName == "file_ext") prefix = "E";
    else if (fieldName == "pinyin") prefix = "P";
    else if (fieldName == "pinyin_acronym") prefix = "A";

    Xapian::QueryParser parser;
    parser.add_prefix(fieldName.toStdString(), prefix);

    int flags = Xapian::QueryParser::FLAG_DEFAULT;
    if (allowWildcard) {
        flags |= Xapian::QueryParser::FLAG_WILDCARD;
    }

    std::string queryStr = fieldName.toStdString() + ":" + (caseSensitive ? keyword.toStdString() : keyword.toLower().toStdString());
    return parser.parse_query(queryStr, flags);
}

Xapian::Query QueryBuilder::buildSimpleQuery(const QString &keyword, bool caseSensitive) const
{
    return buildCommonQuery(keyword, caseSensitive, false);
}

Xapian::Query QueryBuilder::buildWildcardQuery(const QString &keyword, bool caseSensitive) const
{
    return buildCommonQuery(keyword, caseSensitive, true);
}

Xapian::Query QueryBuilder::buildBooleanQuery(const QStringList &terms, bool caseSensitive, SearchQuery::BooleanOperator op) const
{
    if (terms.isEmpty()) {
        return Xapian::Query();
    }

    std::vector<Xapian::Query> subQueries;
    for (const QString &term : terms) {
        if (!term.isEmpty()) {
            Xapian::Query termQuery = buildCommonQuery(term, caseSensitive, false);
            if (!termQuery.empty()) {
                subQueries.push_back(termQuery);
            }
        }
    }

    if (subQueries.empty()) {
        return Xapian::Query();
    }

    return Xapian::Query(op == SearchQuery::BooleanOperator::AND ? Xapian::Query::OP_AND : Xapian::Query::OP_OR, subQueries.begin(), subQueries.end());
}

//--------------------------------------------------------------------
// IndexManager 实现
//--------------------------------------------------------------------

IndexManager::IndexManager()
{
}

Xapian::Database IndexManager::getDatabase(const QString &indexPath) const
{
    if (m_cachedIndexPath == indexPath) {
        return m_cachedDatabase;
    }

    try {
        m_cachedDatabase = Xapian::Database(indexPath.toStdString());
        m_cachedIndexPath = indexPath;
        return m_cachedDatabase;
    } catch (const Xapian::Error &e) {
        qWarning() << "Failed to open Xapian database:" << QString::fromStdString(e.get_msg());
        m_cachedDatabase = Xapian::Database(); // Reset
        m_cachedIndexPath.clear();
        return m_cachedDatabase;
    }
}

//--------------------------------------------------------------------
// FileNameIndexedStrategy 实现
//--------------------------------------------------------------------

FileNameIndexedStrategy::FileNameIndexedStrategy(const SearchOptions &options, QObject *parent)
    : FileNameBaseStrategy(options, parent)
{
    m_queryBuilder = std::make_unique<QueryBuilder>();
    m_indexManager = std::make_unique<IndexManager>();
    initializeIndexing();
}

FileNameIndexedStrategy::~FileNameIndexedStrategy() = default;

void FileNameIndexedStrategy::initializeIndexing()
{
    m_indexDir = Global::fileNameIndexDirectory();
    if (!QFileInfo::exists(m_indexDir)) {
        qWarning() << "Index directory does not exist:" << m_indexDir;
    }
}

void FileNameIndexedStrategy::search(const SearchQuery &query)
{
    m_cancelled.store(false);
    m_results.clear();

    if (!QFileInfo::exists(m_indexDir)) {
        emit errorOccurred(SearchError(SearchErrorCode::InternalError));
        emit searchFinished(m_results);
        return;
    }

    // 获取文件类型设置
    FileNameOptionsAPI optionsApi(const_cast<SearchOptions &>(m_options));

    // 执行搜索
    try {
        performIndexSearch(query, optionsApi);
    } catch (const Xapian::Error &e) {
        qWarning() << "Xapian search exception:" << QString::fromStdString(e.get_msg());
        emit errorOccurred(SearchError(SearchErrorCode::InternalError));
    } catch (const std::exception &e) {
        qWarning() << "Standard exception:" << e.what();
        emit errorOccurred(SearchError(SearchErrorCode::InternalError));
    }

    emit searchFinished(m_results);
}

void FileNameIndexedStrategy::performIndexSearch(const SearchQuery &query, const FileNameOptionsAPI &api)
{
    BaseSearchStrategy::SearchCancellationGuard guard(&m_cancelled);

    bool caseSensitive = m_options.caseSensitive();
    const QString &searchPath = m_options.searchPath();
    const QStringList &searchExcludedPaths = m_options.searchExcludedPaths();

    QStringList fileTypes = api.fileTypes();
    QStringList fileExtensions = api.fileExtensions();
    bool pinyinEnabled = api.pinyinEnabled();
    bool pinyinAcronymEnabled = api.pinyinAcronymEnabled();

    // 1. 确定搜索类型
    SearchType searchType = determineSearchType(query, pinyinEnabled, pinyinAcronymEnabled, fileTypes, fileExtensions);

    // 2. 构建查询
    IndexQuery indexQuery = buildIndexQuery(query, searchType, caseSensitive, pinyinEnabled, pinyinAcronymEnabled, fileTypes, fileExtensions);

    // 3. 执行查询并处理结果
    executeIndexQuery(indexQuery, searchPath, searchExcludedPaths);
}

FileNameIndexedStrategy::SearchType FileNameIndexedStrategy::determineSearchType(
        const SearchQuery &query,
        bool pinyinEnabled,
        bool pinyinAcronymEnabled,
        const QStringList &fileTypes,
        const QStringList &fileExtensions) const
{
    QString keyword = query.keyword();
    bool hasKeyword = !keyword.isEmpty();
    bool hasFileTypes = !fileTypes.isEmpty();
    bool hasFileExts = !fileExtensions.isEmpty();
    bool isBoolean = (query.type() == SearchQuery::Type::Boolean);

    // 检查是否需要组合搜索
    bool combinedWithTypes = (hasKeyword || isBoolean) && (hasFileTypes || hasFileExts);
    if (combinedWithTypes) {
        return SearchType::Combined;
    }

    // 空关键词但有文件类型，使用文件类型搜索
    if (!hasKeyword && hasFileTypes) {
        return SearchType::FileType;
    }

    // 空关键词但有文件后缀，使用文件后缀搜索
    if (!hasKeyword && hasFileExts) {
        return SearchType::FileExt;
    }

    // 通配符查询类型（显式指定）
    if (query.type() == SearchQuery::Type::Wildcard) {
        return SearchType::Wildcard;
    }

    // 布尔查询
    if (isBoolean) {
        return SearchType::Boolean;
    }

    if ((pinyinEnabled || pinyinAcronymEnabled) && !isBoolean) {
        // 如果同时启用了拼音和拼音首字母
        if (pinyinEnabled && pinyinAcronymEnabled) {
            // 检查关键词是否为有效拼音序列
            if (hasKeyword && Global::isPinyinSequence(keyword)) {
                return SearchType::Pinyin;
            } else if (hasKeyword && Global::isPinyinAcronymSequence(keyword)) {
                // 不是有效拼音序列，但是有效的拼音首字母，fallback到拼音首字母搜索
                return SearchType::PinyinAcronym;
            } else {
                // 既不是拼音也不是有效的拼音首字母，使用简单搜索
                return SearchType::Simple;
            }
        }
        // 只启用拼音搜索
        else if (pinyinEnabled) {
            return SearchType::Pinyin;
        }
        // 只启用拼音首字母搜索
        else if (pinyinAcronymEnabled) {
            return SearchType::PinyinAcronym;
        }
    }

    // 默认简单搜索
    return SearchType::Simple;
}

FileNameIndexedStrategy::IndexQuery FileNameIndexedStrategy::buildIndexQuery(
        const SearchQuery &query,
        SearchType searchType,
        bool caseSensitive,
        bool pinyinEnabled,
        bool pinyinAcronymEnabled,
        const QStringList &fileTypes,
        const QStringList &fileExtensions)
{
    IndexQuery result;
    result.type = searchType;
    result.caseSensitive = caseSensitive;
    result.fileTypes = fileTypes;
    result.fileExtensions = fileExtensions;
    result.usePinyin = pinyinEnabled;   // 设置拼音搜索标志
    result.usePinyinAcronym = pinyinAcronymEnabled;   // 设置拼音首字母搜索标志

    switch (searchType) {
    case SearchType::Simple:
        result.terms.append(query.keyword());
        break;
    case SearchType::Wildcard:
        result.terms.append(query.keyword());
        break;
    case SearchType::Boolean:
        result.terms = SearchUtility::extractBooleanKeywords(query);
        result.booleanOp = query.type() == SearchQuery::Type::Boolean ? query.booleanOperator() : SearchQuery::BooleanOperator::AND;
        break;
    case SearchType::Pinyin:
        result.terms.append(query.keyword());
        break;
    case SearchType::PinyinAcronym:
        result.terms.append(query.keyword());
        break;
    case SearchType::FileType:
        result.fileTypes = fileTypes;
        break;
    case SearchType::FileExt:
        result.fileExtensions = fileExtensions;
        break;
    case SearchType::Combined:
        result.terms = query.type() == SearchQuery::Type::Boolean ? SearchUtility::extractBooleanKeywords(query) : QStringList { query.keyword() };
        result.booleanOp = query.type() == SearchQuery::Type::Boolean ? query.booleanOperator() : SearchQuery::BooleanOperator::AND;
        result.combineWithFileType = !fileTypes.isEmpty();
        result.combineWithFileExt = !fileExtensions.isEmpty();
        break;
    }

    return result;
}

void FileNameIndexedStrategy::executeIndexQuery(const IndexQuery &query, const QString &searchPath, const QStringList &searchExcludedPaths)
{
    // 获取 Xapian 数据库
    Xapian::Database db;
    try {
        db = m_indexManager->getDatabase(m_indexDir);
        // Test if db is valid
        db.get_doccount();
    } catch (...) {
        emit errorOccurred(SearchError(SearchErrorCode::InternalError));
        return;
    }

    // 构建 Xapian 查询
    Xapian::Query xapianQuery;
    try {
        xapianQuery = buildXapianQuery(query, searchPath);
        if (xapianQuery.empty()) {
            emit errorOccurred(SearchError(SearchErrorCode::InvalidQuery));
            return;
        }
    } catch (const Xapian::Error &e) {
        qWarning() << "Error building Xapian query:" << QString::fromStdString(e.get_msg());
        emit errorOccurred(SearchError(SearchErrorCode::InvalidQuery));
        return;
    }

    // Measure the time taken to execute the search
    QElapsedTimer searchTimer;
    searchTimer.start();

    // 执行搜索
    Xapian::Enquire enquire(db);
    enquire.set_query(xapianQuery);

    Xapian::MSet mset;
    try {
        int32_t maxResults = m_options.maxResults() > 0 ? m_options.maxResults() : 10000;
        mset = enquire.get_mset(0, maxResults);
        qInfo() << "Filename search execution time:" << searchTimer.elapsed() << "ms"
                << "Total hits:" << mset.get_matches_estimated()
                << "Collected:" << mset.size();
    } catch (const Xapian::Error &e) {
        qWarning() << "Xapian search exception:" << QString::fromStdString(e.get_msg());
        emit errorOccurred(SearchError(SearchErrorCode::InternalError));
        return;
    }

    // Measure the time taken to process search results
    QElapsedTimer resultTimer;
    resultTimer.start();
    m_results.reserve(mset.size());

    // 实时处理搜索结果
    for (Xapian::MSetIterator it = mset.begin(); it != mset.end(); ++it) {
        if (m_cancelled.load()) {
            qInfo() << "Filename search cancelled";
            break;
        }

        try {
            Xapian::Document doc = it.get_document();
            // 假设路径存储在数据字段中，或者作为一个特定的值
            // 在 Xapian 中，通常将元数据存储在 document data 中
            // 这里假设 data 是一个 JSON 字符串或简单的路径
            QString path = QString::fromStdString(doc.get_data());

            if (!path.startsWith(searchPath)) {
                continue;
            }

            if (std::any_of(searchExcludedPaths.cbegin(), searchExcludedPaths.cend(),
                            [&path](const auto &excluded) { return path.startsWith(excluded); })) {
                continue;
            }

            // Xapian 不直接支持 is_hidden 过滤，除非在索引时作为 term 或 value
            // 这里假设我们在查询阶段已经过滤了隐藏文件，或者在结果处理阶段过滤

            // 处理搜索结果
            if (Q_UNLIKELY(m_options.detailedResultsEnabled())) {
                // 假设这些信息也存储在 doc 中
                // 这里使用占位符，实际需要根据索引结构获取
                QString type = ""; // it.get_document().get_value(SLOT_FILE_TYPE)
                QString time = ""; // it.get_document().get_value(SLOT_MODIFY_TIME)
                QString size = ""; // it.get_document().get_value(SLOT_FILE_SIZE)
                m_results.append(processSearchResult(path, type, time, size));
            } else {
                SearchResult result(path);
                m_results.append(result);
            }

            // 实时发送结果
            if (Q_UNLIKELY(m_options.resultFoundEnabled()))
                emit resultFound(m_results.last());

        } catch (const Xapian::Error &e) {
            qWarning() << "Error processing result:" << QString::fromStdString(e.get_msg());
            continue;
        }
    }

    qInfo() << "Filename result processing time:" << resultTimer.elapsed() << "ms";
}

SearchResult FileNameIndexedStrategy::processSearchResult(const QString &path, const QString &type, const QString &time, const QString &size)
{
    // 创建搜索结果
    SearchResult result(path);

    FileNameResultAPI api(result);
    api.setSize(size);
    api.setModifiedTime(time);
    api.setIsDirectory(type == "dir");
    api.setFileType(type);

    return result;
}

Xapian::Query FileNameIndexedStrategy::buildXapianQuery(const IndexQuery &query, const QString &searchPath) const
{
    std::vector<Xapian::Query> mustQueries;
    bool hasValidQuery = false;

    switch (query.type) {
    case SearchType::Simple:
        if (!query.terms.isEmpty()) {
            Xapian::Query simpleQuery = m_queryBuilder->buildSimpleQuery(query.terms.first(), query.caseSensitive);
            if (!simpleQuery.empty()) {
                mustQueries.push_back(simpleQuery);
                hasValidQuery = true;
            }
        }
        break;
    case SearchType::Wildcard:
        if (!query.terms.isEmpty()) {
            Xapian::Query wildcardQuery = m_queryBuilder->buildWildcardQuery(query.terms.first(), query.caseSensitive);
            if (!wildcardQuery.empty()) {
                mustQueries.push_back(wildcardQuery);
                hasValidQuery = true;
            }
        }
        break;
    case SearchType::Boolean:
        if (!query.terms.isEmpty()) {
            Xapian::Query booleanQuery = buildBooleanTermsQuery(query);
            if (!booleanQuery.empty()) {
                mustQueries.push_back(booleanQuery);
                hasValidQuery = true;
            }
        }
        break;
    case SearchType::Pinyin:
        if (!query.terms.isEmpty()) {
            std::vector<Xapian::Query> shouldQueries;

            // 添加拼音查询
            if (Global::isPinyinSequence(query.terms.first())) {
                Xapian::Query pinyinQuery = m_queryBuilder->buildPinyinQuery(query.terms);
                if (!pinyinQuery.empty()) {
                    shouldQueries.push_back(pinyinQuery);
                    hasValidQuery = true;
                }
            }

            // 添加普通关键词查询
            Xapian::Query simpleQuery = m_queryBuilder->buildSimpleQuery(query.terms.first(), query.caseSensitive);
            if (!simpleQuery.empty()) {
                shouldQueries.push_back(simpleQuery);
                hasValidQuery = true;
            }

            if (!shouldQueries.empty()) {
                mustQueries.push_back(Xapian::Query(Xapian::Query::OP_OR, shouldQueries.begin(), shouldQueries.end()));
            }
        }
        break;
    case SearchType::PinyinAcronym:
        if (!query.terms.isEmpty()) {
            std::vector<Xapian::Query> shouldQueries;

            // 添加拼音首字母查询
            if (Global::isPinyinAcronymSequence(query.terms.first())) {
                Xapian::Query pinyinAcronymQuery = m_queryBuilder->buildPinyinAcronymQuery(query.terms);
                if (!pinyinAcronymQuery.empty()) {
                    shouldQueries.push_back(pinyinAcronymQuery);
                    hasValidQuery = true;
                }
            }

            // 添加普通关键词查询
            Xapian::Query simpleQuery = m_queryBuilder->buildSimpleQuery(query.terms.first(), query.caseSensitive);
            if (!simpleQuery.empty()) {
                shouldQueries.push_back(simpleQuery);
                hasValidQuery = true;
            }

            if (!shouldQueries.empty()) {
                mustQueries.push_back(Xapian::Query(Xapian::Query::OP_OR, shouldQueries.begin(), shouldQueries.end()));
            }
        }
        break;
    case SearchType::FileType:
        if (!query.fileTypes.isEmpty()) {
            Xapian::Query typeQuery = m_queryBuilder->buildTypeQuery(query.fileTypes);
            if (!typeQuery.empty()) {
                mustQueries.push_back(typeQuery);
                hasValidQuery = true;
            }
        }
        break;
    case SearchType::FileExt:
        if (!query.fileExtensions.isEmpty()) {
            Xapian::Query extQuery = m_queryBuilder->buildExtQuery(query.fileExtensions);
            if (!extQuery.empty()) {
                mustQueries.push_back(extQuery);
                hasValidQuery = true;
            }
        }
        break;
    case SearchType::Combined:
        if (!query.terms.isEmpty()) {
            Xapian::Query combinedQuery = buildBooleanTermsQuery(query);
            if (!combinedQuery.empty()) {
                mustQueries.push_back(combinedQuery);
                hasValidQuery = true;
            }
        }

        // 构建文件类型查询
        if (query.combineWithFileType && !query.fileTypes.isEmpty()) {
            Xapian::Query typeQuery = m_queryBuilder->buildTypeQuery(query.fileTypes);
            if (!typeQuery.empty()) {
                mustQueries.push_back(typeQuery);
                hasValidQuery = true;
            }
        }

        // 构建文件后缀查询
        if (query.combineWithFileExt && !query.fileExtensions.isEmpty()) {
            Xapian::Query extQuery = m_queryBuilder->buildExtQuery(query.fileExtensions);
            if (!extQuery.empty()) {
                mustQueries.push_back(extQuery);
                hasValidQuery = true;
            }
        }
        break;
    }

    // Path prefix optimization
    if (hasValidQuery && SearchUtility::shouldUsePathPrefixQuery(searchPath)) {
        // Assume path is indexed with prefix 'P'
        Xapian::Query pathQuery("P" + searchPath.toStdString());
        mustQueries.push_back(pathQuery);
    }

    // Hidden files
    if (hasValidQuery && Q_LIKELY(!m_options.includeHidden())) {
        Xapian::Query hiddenQuery("H" + std::string("Y"));
        mustQueries.push_back(Xapian::Query(Xapian::Query::OP_AND_NOT, Xapian::Query(), hiddenQuery));
    }

    if (mustQueries.empty()) return Xapian::Query();
    return Xapian::Query(Xapian::Query::OP_AND, mustQueries.begin(), mustQueries.end());
}

Xapian::Query FileNameIndexedStrategy::buildBooleanTermsQuery(const IndexQuery &query) const
{
    std::vector<Xapian::Query> mustQueries;
    bool hasValidQuery = false;

    // 对每个搜索词创建子查询
    for (const QString &term : query.terms) {
        if (term.isEmpty()) continue;

        std::vector<Xapian::Query> shouldQueries;

        // 1. 普通关键词查询
        Xapian::Query keywordQuery = m_queryBuilder->buildSimpleQuery(term, query.caseSensitive);
        if (!keywordQuery.empty()) {
            shouldQueries.push_back(keywordQuery);
        }

        // 2. 拼音查询
        if (query.usePinyin && Global::isPinyinSequence(term)) {
            Xapian::Query pinyinQuery = m_queryBuilder->buildPinyinQuery(QStringList { term });
            if (!pinyinQuery.empty()) {
                shouldQueries.push_back(pinyinQuery);
            }
        }

        // 3. 拼音首字母查询
        if (query.usePinyinAcronym && Global::isPinyinAcronymSequence(term)) {
            Xapian::Query pinyinAcronymQuery = m_queryBuilder->buildPinyinAcronymQuery(QStringList { term });
            if (!pinyinAcronymQuery.empty()) {
                shouldQueries.push_back(pinyinAcronymQuery);
            }
        }

        if (!shouldQueries.empty()) {
            Xapian::Query termQuery(Xapian::Query::OP_OR, shouldQueries.begin(), shouldQueries.end());
            mustQueries.push_back(termQuery);
            hasValidQuery = true;
        }
    }

    if (!hasValidQuery) return Xapian::Query();
    return Xapian::Query(query.booleanOp == SearchQuery::BooleanOperator::AND ? Xapian::Query::OP_AND : Xapian::Query::OP_OR, mustQueries.begin(), mustQueries.end());
}

void FileNameIndexedStrategy::cancel()
{
    m_cancelled.store(true);
}

DFM_SEARCH_END_NS
