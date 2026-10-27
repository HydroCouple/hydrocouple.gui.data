/**
 * @file CatalogueFilter.hpp
 * @brief Local, literal property filtering over reviewed catalogue identities.
 * @details Filtering never changes the source query, stored records or review authority.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QJsonValue>
#include <QSortFilterProxyModel>

namespace OpenSWMM::Data
{
/** @brief Filter saved properties without evaluating regular expressions or scripts. */
class CatalogueFilter final : public QSortFilterProxyModel
{
public:
    /** @brief Source column-zero role holding the complete saved property object. */
    static constexpr int PropertiesRole = Qt::UserRole + 1;
    /** @brief Available literal, numeric and presence comparisons. */
    enum class Match { Contains, Equals, AtLeast, AtMost, HasValue, IsMissing };
    /** @brief Construct a proxy owned by its panel.
     * @param[in] parent Optional owning QObject. */
    explicit CatalogueFilter(QObject *parent = nullptr);
    /** @brief Apply one local predicate to owning source properties.
     * @param[in] field Exact property name; empty searches every property value.
     * @param[in] match Literal text, numeric or presence operation.
     * @param[in] text Comparison value; ignored for presence operations. */
    void setCriteria(const QString &field, Match match, const QString &text);
    /** @brief Format a property faithfully for inspection and literal matching.
     * @param[in] value Original JSON property, including arrays, objects or null.
     * @return Plain text; nested values use compact JSON and null remains explicit. */
    static QString attributeText(const QJsonValue &value);

protected:
    /** @brief Test one source record, never a sorted/filtered row number.
     * @param[in] sourceRow Source-model row.
     * @param[in] sourceParent Source-model parent.
     * @return True when its properties match the current predicate. */
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    /** @brief Compare one JSON value using the currently selected operation.
     * @param[in] value Saved property or undefined when absent.
     * @return Whether that value satisfies the predicate. */
    bool matches(const QJsonValue &value) const;
    QString mField;                 ///< Exact saved-property key, or empty for any value.
    QString mText;                  ///< Literal text or locale-independent numeric input.
    Match mMatch = Match::Contains; ///< Current comparison operation.
};
}
