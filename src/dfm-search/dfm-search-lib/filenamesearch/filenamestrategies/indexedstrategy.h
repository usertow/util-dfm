// SPDX-FileCopyrightText: 2025 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FILENAME_INDEXED_STRATEGY_H
#define FILENAME_INDEXED_STRATEGY_H

#include "basestrategy.h"

#include <memory>

#include <xapian.h>

#include <dfm-search/searchquery.h>

DFM_SEARCH_BEGIN_NS

class QueryBuilder;
class SearchCache;
class IndexManager;
/**
 * @brief 文件名索引搜索策略
 * 使用 Xapian 实现高性能文件搜索
 */
class FileNameIndexedStrategy : public FileNameBaseStrategy
{
    Q_OBJECT

public:
    // 内部使用的搜索类型枚举
    enum class SearchType {
        Simple,   // 简单关键词搜索
        Wildcard,   // 通配符搜索
        Boolean,   // 布尔多关键词搜索
        Pinyin,   // 拼音搜索
        PinyinAcronym,   // 拼音首字母搜索
        FileType,   // 文件类型搜索
        FileExt,   // 文件后缀搜索
        Combined   // 组合搜索(关键词+文件类型/拼音/文件后缀)
    };

    // 索引查询结构
    struct IndexQuery
    {
        SearchType type = SearchType::Simple;
        QStringList terms;
        QStringList fileTypes;
        QStringList fileExtensions;
        bool caseSensitive = false;
        bool usePinyin = false;
        bool usePinyinAcronym = false;
        SearchQuery::BooleanOperator booleanOp = SearchQuery::BooleanOperator::AND;
        bool combineWithFileType = false;   // 是否与文件类型组合
        bool combineWithFileExt = false;   // 是否与文件后缀组合
    };

    explicit FileNameIndexedStrategy(const SearchOptions &options, QObject *parent = nullptr);
    ~FileNameIndexedStrategy() override;

    void search(const SearchQuery &query) override;
    void cancel() override;

private:
    // 初始化索引相关设置
    void initializeIndexing();

    // 执行索引搜索
    void performIndexSearch(const SearchQuery &query, const FileNameOptionsAPI &api);

    // 确定搜索类型
    SearchType determineSearchType(const SearchQuery &query,
                                   bool pinyinEnabled,
                                   bool pinyinAcronymEnabled,
                                   const QStringList &fileTypes,
                                   const QStringList &fileExtensions) const;

    // 构建索引查询
    IndexQuery buildIndexQuery(const SearchQuery &query,
                               SearchType searchType,
                               bool caseSensitive,
                               bool pinyinEnabled,
                               bool pinyinAcronymEnabled,
                               const QStringList &fileTypes,
                               const QStringList &fileExtensions);

    // 执行索引查询并处理结果
    void executeIndexQuery(const IndexQuery &query, const QString &searchPath, const QStringList &searchExcludedPaths);

    // 构建 Xapian 查询
    Xapian::Query buildXapianQuery(const IndexQuery &query, const QString &searchPath) const;

    // 构建布尔查询的辅助方法
    Xapian::Query buildBooleanTermsQuery(const IndexQuery &query) const;

    // 处理搜索结果
    SearchResult processSearchResult(const QString &path, const QString &type, const QString &time, const QString &size);

    // 成员变量
    QString m_indexDir;   // 索引目录路径

    // Xapian 相关组件
    std::unique_ptr<QueryBuilder> m_queryBuilder;   // 查询构建器
    std::unique_ptr<IndexManager> m_indexManager;   // 索引管理器
};

/**
 * @brief 查询构建器类
 * 负责构建各种类型的 Xapian 查询
 */
class QueryBuilder
{
public:
    QueryBuilder();

    // 构建各种类型的查询
    Xapian::Query buildTypeQuery(const QStringList &types) const;
    Xapian::Query buildExtQuery(const QStringList &extensions) const;
    Xapian::Query buildPinyinQuery(const QStringList &pinyins, SearchQuery::BooleanOperator op = SearchQuery::BooleanOperator::AND) const;
    Xapian::Query buildPinyinAcronymQuery(const QStringList &acronyms, SearchQuery::BooleanOperator op = SearchQuery::BooleanOperator::AND) const;
    Xapian::Query buildBooleanQuery(const QStringList &terms, bool caseSensitive, SearchQuery::BooleanOperator op) const;
    Xapian::Query buildWildcardQuery(const QString &keyword, bool caseSensitive) const;
    Xapian::Query buildSimpleQuery(const QString &keyword, bool caseSensitive) const;

    // 通用的查询构建方法
    Xapian::Query buildCommonQuery(const QString &keyword, bool caseSensitive, bool allowWildcard = false) const;
    Xapian::Query buildCommonQuery(const QString &keyword, bool caseSensitive, const QString &fieldName, bool allowWildcard = false) const;

private:
};

/**
 * @brief 索引管理器类
 * 管理 Xapian 数据库
 */
class IndexManager
{
public:
    IndexManager();

    // 获取 Xapian 数据库
    Xapian::Database getDatabase(const QString &indexPath) const;

private:
    mutable Xapian::Database m_cachedDatabase;
    mutable QString m_cachedIndexPath;
};

DFM_SEARCH_END_NS

#endif   // FILENAME_INDEXED_STRATEGY_H
