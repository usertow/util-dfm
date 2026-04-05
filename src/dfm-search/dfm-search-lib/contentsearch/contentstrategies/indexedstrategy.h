// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CONTENT_INDEXED_STRATEGY_H
#define CONTENT_INDEXED_STRATEGY_H

#include "basestrategy.h"

#include <xapian.h>

// 前向声明
class ContentSearcher;

DFM_SEARCH_BEGIN_NS

/**
 * @brief 内容索引搜索策略
 */
class ContentIndexedStrategy : public ContentBaseStrategy
{
    Q_OBJECT

public:
    explicit ContentIndexedStrategy(const SearchOptions &options, QObject *parent = nullptr);
    ~ContentIndexedStrategy() override;

    void search(const SearchQuery &query) override;
    void cancel() override;

private:
    // 初始化索引
    void initializeIndexing();

    // 执行内容搜索
    void performContentSearch(const SearchQuery &query);

    // Build Xapian query
    Xapian::Query buildXapianQuery(const SearchQuery &query, const QString &searchPath);
    // Helper for simple queries (original logic for "contents" field)
    Xapian::Query buildSimpleContentsQuery(
            const SearchQuery &query,
            Xapian::QueryParser &contentsParser);

    // Helper for "standard" boolean logic (original logic for "contents" field, handles AND/OR)
    Xapian::Query buildStandardBooleanContentsQuery(
            const SearchQuery &query,
            Xapian::QueryParser &contentsParser);

    // Helper for "advanced" mixed AND logic (searches "contents" and "filename")
    Xapian::Query buildAdvancedAndQuery(
            const SearchQuery &query,   // Operator is implicitly AND
            Xapian::QueryParser &contentsParser);

    // Process search results
    void processSearchResults(const Xapian::MSet &mset);

    QString m_indexDir;
    Xapian::Query m_currentQuery;   // 存储当前查询
    QStringList m_keywords;
};

DFM_SEARCH_END_NS

#endif   // CONTENT_INDEXED_STRATEGY_H
