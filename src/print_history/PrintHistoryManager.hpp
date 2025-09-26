#ifndef PRINTHISTORYMANAGER_HPP
#define PRINTHISTORYMANAGER_HPP

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QVector>

class PrintHistoryManager {
public:
    PrintHistoryManager(const QString& dbName);
    ~PrintHistoryManager();
    void addPrintJob(const QString& jobDetails);
    QVector<QString> getPrintHistory();

private:
    QSqlDatabase db;
};

#endif // PRINTHISTORYMANAGER_HPP