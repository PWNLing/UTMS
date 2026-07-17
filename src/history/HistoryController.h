#pragma once

#include <memory>

#include <QObject>

#include "history/HistoryStore.h"

namespace utms {

class HistoryController : public QObject
{
    Q_OBJECT

public:
    explicit HistoryController(QObject *parent = nullptr);

public slots:
    void initialize(const QString &database_path);
    void startSession();
    void stopSession();
    void saveConfiguration(const utms::HistoryConfiguration &configuration);
    void shutdown();

signals:
    void configurationLoaded(const utms::HistoryConfiguration &configuration);
    void availabilityChanged(bool available, const QString &detail);
    void sessionActiveChanged(bool active, const QString &detail);
    void errorOccurred(const QString &message);
    void stopped();

private:
    void reportError(const QString &message);

    std::unique_ptr<HistoryStore> store_;
    bool initialized_ = false;
    bool session_active_ = false;
};

} // namespace utms
