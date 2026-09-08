#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace hftrec::gui::detail {

struct FinamSymbolRow {
    QString symbol;
    QString ticker;
    QString name;
    QString mic;
    QString market;
    QString type;
    QString underlying;
    QString expiration;
    QString contractSize;
    bool isArchived{false};
};

struct FinamCatalog {
    std::vector<FinamSymbolRow> rows;
    std::vector<const FinamSymbolRow*> spotRows;
    std::vector<const FinamSymbolRow*> futuresRows;
    QHash<QString, const FinamSymbolRow*> bySymbol;
    QHash<QString, const FinamSymbolRow*> byTicker;
    QHash<QString, std::vector<const FinamSymbolRow*>> futuresByUnderlying;
};

const FinamCatalog& finamCatalog();
const std::vector<FinamSymbolRow>& finamSymbolRows();
const std::vector<const FinamSymbolRow*>& finamRowsForMarket(const QString& market);
bool finamRowMatchesMarket(const FinamSymbolRow& row, const QString& market);
bool finamTextMatchesQuery(const FinamSymbolRow& row, const QString& query);
QString finamAnchorUnderlying(const QString& anchorSymbolText);
int finamSuggestionRank(const FinamSymbolRow& row,
                        const QString& venueMarket,
                        const QString& anchorMarket,
                        const QString& anchorUnderlying);
QVariantMap finamSuggestionItem(const FinamSymbolRow& row, int rank);

}  // namespace hftrec::gui::detail
