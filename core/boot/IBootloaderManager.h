#pragma once

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace kh::boot {

struct BootEntry {
    QString entryId;
    QString kernelVersion;
    QString title;
    bool isDefault = false;
    bool isCurrentlyRunning = false;
    int bootOrderIndex = -1;
};

class IBootloaderManager : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IBootloaderManager() override = default;

    virtual void listBootEntries() = 0;
    virtual void setDefaultEntry(const QString &entryId) = 0;
    virtual void regenerateConfig() = 0;
    virtual void rebootIntoEntryOnce(const QString &entryId) = 0;

signals:
    void bootEntriesListed(QList<kh::boot::BootEntry> entries);
    void bootEntriesFailed(QString error);
    void operationFinished(bool success, QString error);
};

}  // namespace kh::boot

Q_DECLARE_METATYPE(kh::boot::BootEntry)

