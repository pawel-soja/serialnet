#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSerialPort>
#include <QDebug>

#include <QUdpSocket>
#include <QNetworkDatagram>

#include <QTcpServer>
#include <QTcpSocket>

#include <QLoggingCategory>

#include <QWebSocketServer>
#include <QWebSocket>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QLoggingCategory log("serialnet");

    QCoreApplication::setApplicationName("serialnet");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::tr("Serial port via udp server"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOptions({
        {{"d", "device"},      QCoreApplication::tr("Device path, default: /dev/serial0"),  QCoreApplication::tr("path"),     "/dev/serial0"},
        {{"b", "baud"},        QCoreApplication::tr("Serial port baudrade, default: 9600"), QCoreApplication::tr("baudrate"), "9600"},
        {{"e", "echo"},        QCoreApplication::tr("Reply request message to client")},
        {{"r", "cr-flush"},    QCoreApplication::tr("Flush the data from the serial port when a carriage return occurs")},
        {{"n", "lf-flush"},    QCoreApplication::tr("Flush the data from the serial port when a line feed occurs")},
        {"udp-port",           QCoreApplication::tr("Listen on UDP port"), "port"},
        {"tcp-port",           QCoreApplication::tr("Listen on TCP port"), "port"},
        {"ws-port",            QCoreApplication::tr("Listen on WebSocket port (binnary data)"), "port"},
        {"fakeserial",         QCoreApplication::tr("Don't open serial port")},
        {"verbose",            QCoreApplication::tr("Verbose")}
    });
    parser.process(a);

    log.setEnabled(QtMsgType::QtDebugMsg, parser.isSet("verbose"));

    // Serial Port
    bool echo       = parser.isSet("echo");
    QString device  = parser.value("device");
    bool crFlush    = parser.isSet("cr-flush");
    bool lfFlush    = parser.isSet("lf-flush");

    QSerialPort serial;

    // Network Parameters
    QHostAddress address(QHostAddress::Any);
    quint16 udpPort = parser.value("udp-port").toUShort();
    quint16 tcpPort = parser.value("tcp-port").toUShort();
    quint16 wsPort  = parser.value("ws-port").toUShort();

    // UDP
    QUdpSocket          udpServer;
    QHostAddress        udpActiveAddress;
    quint16             udpActivePort = 0;

    // TCP
    QTcpServer          tcpServer;
    QList<QTcpSocket *> tcpClientList;

    // WebSocket
    QWebSocketServer    wsServer("serialnet", QWebSocketServer::NonSecureMode);
    QList<QWebSocket *> wsClientList;

    serial.setPortName(device);
    serial.setBaudRate(parser.value("baud").toInt());

    // Send data to all network clients
    auto replyToAll = [&](const QByteArray &data)
    {
        qDebug(log).noquote().nospace()
            << "Serial -> ALL"
            << " " << data.toHex(' ');

        if (udpActivePort != 0)
            udpServer.writeDatagram(data, udpActiveAddress, udpActivePort);

        for(auto client: tcpClientList)
            client->write(data);

        for(auto client: wsClientList)
            client->sendBinaryMessage(data);
    };

    // Send received data to all
    auto broadcast = [&](const QByteArray &data)
    {
        if (echo)
            replyToAll(data);

        if (serial.isOpen())
            serial.write(data);
    };

    // Serial Port Events
    QObject::connect(&serial, &QSerialPort::readyRead, [&](){
        if (!crFlush && !lfFlush)
        {
            replyToAll(serial.readAll());
            return;
        }

        int index;
        while (lfFlush && (index = serial.peek(1024).indexOf('\n')) >= 0)
            replyToAll(serial.read(index + 1));

        while (crFlush && (index = serial.peek(1024).indexOf('\r')) >= 0)
            replyToAll(serial.read(index + 1));
    });

    // Udp Server Events
    QObject::connect(&udpServer, &QUdpSocket::readyRead, [&](){
        while (udpServer.hasPendingDatagrams()) {
            QNetworkDatagram datagram = udpServer.receiveDatagram();
            udpActiveAddress = datagram.senderAddress();
            udpActivePort = quint16(datagram.senderPort());
            qDebug(log).noquote().nospace()
                << "Serial <- UDP"
                //<< " " << udpActiveAddress.toString() << ":" << udpActivePort
                << " " << datagram.data().toHex(' ');
            broadcast(datagram.data());
        }
    });

    // Tcp Server Events
    QObject::connect(&tcpServer, &QTcpServer::newConnection, [&](){
        while (tcpServer.hasPendingConnections())
        {
            QTcpSocket *client = tcpServer.nextPendingConnection();
            qDebug(log).noquote().nospace()
                << "Open TCP Client"
                << " " << client->peerAddress().toString() << ":" << client->peerPort();
            QObject::connect(client, &QTcpSocket::disconnected, [&, client](){
                qDebug(log).noquote().nospace()
                    << "Close TCP Client"
                    << " " << client->peerAddress().toString() << ":" << client->peerPort();
                tcpClientList.removeAll(client);
            });
            QObject::connect(client, &QTcpSocket::readyRead, [&, client](){
                const QByteArray data = client->readAll();

                qDebug(log).noquote().nospace()
                    << "Serial <- TCP"
                    //<< " " << client->peerAddress().toString() << ":" << client->peerPort()
                    << " " << data.toHex(' ');
                broadcast(data);
            });
            tcpClientList.append(client);
        }
    });

    // WebSocket Server Events
    QObject::connect(&wsServer, &QWebSocketServer::newConnection, [&](){
        while (wsServer.hasPendingConnections())
        {
           QWebSocket *client = wsServer.nextPendingConnection();
           QObject::connect(client, &QWebSocket::disconnected, [&, client](){
               wsClientList.removeAll(client);
           });
           QObject::connect(client, &QWebSocket::binaryMessageReceived, [&](const QByteArray &message){
               broadcast(message);
           });
           wsClientList.append(client);
        }
    });

    // Bind ports
    if (!parser.isSet("fakeserial") && !serial.open(QSerialPort::ReadWrite))
    {
        qCritical(log).noquote().nospace()
            << "Can't open port " << device
            << " : " << serial.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("udp-port") && !udpServer.bind(address, udpPort))
    {
        qCritical(log).noquote().nospace()
            << "Can't grab UDP "
            << address.toString() << ":" << udpPort
            << " : "  << udpServer.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("tcp-port") && !tcpServer.listen(address, tcpPort))
    {
        qCritical(log).noquote().nospace()
            << "Can't grab TCP "
            << address.toString() << ":" << tcpPort
            << " : "  << tcpServer.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("ws-port") && !wsServer.listen(address, wsPort))
    {
        qCritical(log).noquote().nospace()
            << "Can't grab WebSocket "
            << address.toString() << ":" << wsPort
            << " : "  << wsServer.errorString();

        return EXIT_FAILURE;
    }


    return a.exec();
}
