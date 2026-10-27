/**
 * @file CatalogueFilter.cpp
 * @brief Case-insensitive literal and finite numeric catalogue-property predicates.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "CatalogueFilter.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace OpenSWMM::Data
{
CatalogueFilter::CatalogueFilter(QObject *parent) : QSortFilterProxyModel(parent) {}
void CatalogueFilter::setCriteria(const QString &field, Match match, const QString &text)
{
    mField = field;
    mMatch = match;
    mText = text;
    invalidateFilter();
}
QString CatalogueFilter::attributeText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isUndefined())
        return {};
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}
bool CatalogueFilter::matches(const QJsonValue &value) const
{
    const bool present = !value.isUndefined() && !value.isNull();
    if (mMatch == Match::HasValue)
        return present;
    if (mMatch == Match::IsMissing)
        return !present;
    if (!present)
        return false;
    const auto text = attributeText(value);
    if (mMatch == Match::Contains)
        return text.contains(mText, Qt::CaseInsensitive);
    if (mMatch == Match::Equals)
        return text.compare(mText, Qt::CaseInsensitive) == 0;
    bool inputOk = false, propertyOk = false;
    const double input = mText.toDouble(&inputOk);
    const double property = value.isDouble() ? value.toDouble() : text.toDouble(&propertyOk);
    propertyOk = value.isDouble() || (value.isString() && propertyOk);
    if (!inputOk || !propertyOk || !std::isfinite(input) || !std::isfinite(property))
        return false;
    return mMatch == Match::AtLeast ? property >= input : property <= input;
}
bool CatalogueFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (mMatch == Match::Contains && mText.isEmpty())
        return true;
    const auto properties =
        sourceModel()->index(sourceRow, 0, sourceParent).data(PropertiesRole).value<QJsonObject>();
    if (!mField.isEmpty())
        return matches(properties.value(mField));
    for (auto it = properties.begin(); it != properties.end(); ++it)
        if (matches(it.value()))
            return true;
    return false;
}
}
