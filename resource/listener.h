#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QList>
#include <QObject>

class Client
{
public:
    Client(const QString &n, QLocalSocket *s)
        : name(n),
          socket(s),
          hasSentCommand(false)
    {
    }

    QString name;
    QLocalSocket *socket;
    QByteArray commandBuffer;
    bool hasSentCommand;
};

class Listener : public QObject
{
    Q_OBJECT

public:
    Listener(const QString &resourceName, QObject *parent = 0);
    ~Listener();

Q_SIGNALS:
    void noClients();

public Q_SLOTS:
    void closeAllConnections();

private Q_SLOTS:
    void acceptConnection();
    void clientDropped();
    void checkConnections();
    void readFromSocket();

private:
    void processClientBuffer(Client &client);
    QLocalServer *m_server;
    QList<Client> m_connections;
};