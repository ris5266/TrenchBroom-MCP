#include "mcp/McpServer.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>

#include "mcp/Dispatch.h"
#include "mcp/QtHostContext.h"

#include "mdl/Map.h"

#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapViewToolBox.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <nlohmann/json.hpp>

namespace tb::mcp
{
namespace
{

QByteArray toLine(const nlohmann::json& value)
{
  const auto text = value.dump();
  return QByteArray::fromStdString(text) + '\n';
}

QByteArray errorLine(const std::string& code, const std::string& message)
{
  return toLine(
    nlohmann::json{{"ok", false}, {"error", {{"code", code}, {"message", message}}}});
}

bool anyToolActive(const ui::MapViewToolBox& toolBox)
{
  return toolBox.assembleBrushToolActive() || toolBox.clipToolActive()
         || toolBox.rotateToolActive() || toolBox.scaleToolActive()
         || toolBox.shearToolActive() || toolBox.sweepToolActive()
         || toolBox.anyVertexToolActive();
}

}

McpServer::McpServer(ui::AppController& appController, QObject* parent)
  : QObject{parent}
  , m_appController{appController}
{
}

McpServer::~McpServer()
{
  stop();
}

QString McpServer::defaultSocketName()
{
  return QStringLiteral("trenchbroom-mcp-%1").arg(qEnvironmentVariable("USER", "user"));
}

namespace
{

bool socketIsLive(const QString& name)
{
  auto probe = QLocalSocket{};
  probe.connectToServer(name);
  const auto connected = probe.waitForConnected(200);
  probe.abort();
  return connected;
}

}

bool McpServer::start()
{
  if (m_server)
  {
    return true;
  }

  m_server = new QLocalServer{this};
  connect(m_server, &QLocalServer::newConnection, this, &McpServer::onNewConnection);

  const auto name = defaultSocketName();
  if (!m_server->listen(name))
  {
    if (m_server->serverError() != QAbstractSocket::AddressInUseError || socketIsLive(name))
    {
      delete m_server;
      m_server = nullptr;
      emit listeningChanged();
      return false;
    }

    QLocalServer::removeServer(name);
    if (!m_server->listen(name))
    {
      delete m_server;
      m_server = nullptr;
      emit listeningChanged();
      return false;
    }
  }

  emit listeningChanged();
  return true;
}

void McpServer::stop()
{
  if (!m_server)
  {
    return;
  }

  m_buffers.clear();
  m_server->close();
  delete m_server;
  m_server = nullptr;

  emit listeningChanged();
  emit connectionCountChanged();
}

bool McpServer::isListening() const
{
  return m_server && m_server->isListening();
}

QString McpServer::socketName() const
{
  return m_server ? m_server->serverName() : QString{};
}

QString McpServer::errorString() const
{
  return m_server ? m_server->errorString() : QString{};
}

int McpServer::connectionCount() const
{
  return int(m_buffers.size());
}

void McpServer::onNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    m_buffers.insert(socket, {});

    connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
    connect(
      socket, &QLocalSocket::disconnected, this, [this, socket] { onDisconnected(socket); });

    emit connectionCountChanged();
  }
}

void McpServer::onDisconnected(QLocalSocket* socket)
{
  m_buffers.remove(socket);
  socket->deleteLater();

  emit connectionCountChanged();
}

void McpServer::onReadyRead(QLocalSocket* socket)
{
  auto& buffer = m_buffers[socket];
  buffer += socket->readAll();

  for (auto newline = buffer.indexOf('\n'); newline >= 0; newline = buffer.indexOf('\n'))
  {
    const auto requestLine = buffer.left(newline);
    buffer.remove(0, newline + 1);

    if (!requestLine.trimmed().isEmpty())
    {
      socket->write(handleRequest(requestLine));
      socket->flush();
    }
  }
}

QByteArray McpServer::handleRequest(const QByteArray& requestLine)
{
  if (QApplication::activeModalWidget())
  {
    return errorLine(
      "editor_busy", "a dialog is open in TrenchBroom; close it and try again");
  }

  auto* mapWindow = m_appController.mapWindowManager().topMapWindow();
  if (!mapWindow)
  {
    return errorLine("no_document", "no map is open in TrenchBroom");
  }

  if (anyToolActive(mapWindow->toolBox()))
  {
    return errorLine(
      "editor_busy", "an editing tool is active in TrenchBroom; deactivate it and retry");
  }

  auto host = QtHostContext{m_appController, *mapWindow};
  return toLine(
    dispatch(mapWindow->document().map(), requestLine.toStdString(), &host));
}

}
