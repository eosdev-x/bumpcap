#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

#include "core/model/KernelInfo.h"

namespace kh::gui {

class KernelTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        kSourceColumn = 0,
        kVersionColumn,
        kReleasedColumn,
        kStatusColumn,
        kNotesColumn,
        kColumnCount,
    };

    explicit KernelTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    void setKernels(QList<kh::model::KernelInfo> kernels);
    void setSourcePriorities(const QHash<int, int> &source_priorities);
    const QList<kh::model::KernelInfo> &kernels() const;
    kh::model::KernelInfo kernelAt(int row) const;
    bool setKernelPinned(const QString &version, bool pinned);
    bool setKernelNote(const QString &version, const QString &note);

    static QString statusText(kh::model::KernelStatus status);
    static QString accessibleStatusText(const kh::model::KernelInfo &kernel);

private:
    QVariant displayData(const kh::model::KernelInfo &kernel, int column) const;
    QVariant decorationData(const kh::model::KernelInfo &kernel, int column) const;
    QVariant tooltipData(const kh::model::KernelInfo &kernel, int column) const;
    bool lessThan(const kh::model::KernelInfo &left,
                  const kh::model::KernelInfo &right,
                  int column) const;
    int sourcePriority(kh::model::SourceId source_id) const;

    QList<kh::model::KernelInfo> kernels_;
    QHash<int, int> source_priorities_;
    int sort_column_ = kSourceColumn;
    Qt::SortOrder sort_order_ = Qt::AscendingOrder;
};

}  // namespace kh::gui
