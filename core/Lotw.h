#ifndef LOTW_H
#define LOTW_H

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

class Lotw : public QObject
{
    Q_OBJECT
public:
    explicit Lotw(QObject *parent = nullptr);

    void update(QDate start_date, bool qso_since);

signals:
    void updateProgress(int value);
    void updateComplete();
    void updateFailed();

public slots:
    void processReply(QNetworkReply* reply);

private:
    QNetworkAccessManager* nam;

    void get(QList<QPair<QString, QString>> params);
};

#endif // LOTW_H
