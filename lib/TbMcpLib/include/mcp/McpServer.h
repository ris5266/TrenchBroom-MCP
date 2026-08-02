#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace tb::ui
{
class AppController;
}

namespace tb::mcp
{

class McpServer : public QObject
{
  Q_OBJECT
private:
  ui::AppController& m_appController;
  QLocalServer* m_server = nullptr;

  QHash<QLocalSocket*, QByteArray> m_buffers;

public:
  explicit McpServer(ui::AppController& appController, QObject* parent = nullptr);
  ~McpServer() override;

  bool start();
  void stop();

  bool isListening() const;
  QString socketName() const;
  QString errorString() const;

  int connectionCount() const;

  static QString defaultSocketName();

signals:
  void listeningChanged();
  void connectionCountChanged();

private:
  void onNewConnection();
  void onReadyRead(QLocalSocket* socket);
  void onDisconnected(QLocalSocket* socket);

  QByteArray handleRequest(const QByteArray& requestLine);
};

}
