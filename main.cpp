#include <QCoreApplication>
#include <QCommandLineParser>
#include <QSerialPort>
#include <QDebug>

#include <QUdpSocket>
#include <QNetworkDatagram>

#include <QTcpServer>
#include <QTcpSocket>

#include <QWebSocketServer>
#include <QWebSocket>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>


static void daemonize()
{
    pid_t pid;
    pid = fork();

    if (pid < 0)      exit(EXIT_FAILURE);
    if (pid > 0)      exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    pid = fork();

    if (pid < 0)      exit(EXIT_FAILURE);
    if (pid > 0)      exit(EXIT_SUCCESS);

    umask(0);

    chdir("/");

    for (auto x = sysconf(_SC_OPEN_MAX); x>=0; x--)
        close(int(x));
}

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QCoreApplication::setApplicationName("serialnet");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::tr("Serial port via udp server"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOptions({
        {{"d", "device"},      QCoreApplication::tr("Device path, default: /dev/serial0"),  QCoreApplication::tr("path"),     "/dev/serial0"},
        {{"s", "speed"},       QCoreApplication::tr("Serial port baudrade, default: 9600"), QCoreApplication::tr("baudrate"), "9600"},
        {{"e", "echo"},        QCoreApplication::tr("Reply request message to client")},
        {{"b", "background"},  QCoreApplication::tr("Run in background")},
        {"udp-port",           QCoreApplication::tr("Listen on UDP port"), "port"},
        {"tcp-port",           QCoreApplication::tr("Listen on TCP port"), "port"},
        {"ws-port",            QCoreApplication::tr("Listen on WebSocket port (binnary data)"), "port"},
        {"fakeserial",         QCoreApplication::tr("Don't open serial port")},
    });
    parser.process(a);

    if (parser.isSet("background"))
        daemonize();

    // Serial Port
    bool echo       = parser.isSet("echo");
    QString device  = parser.value("device");

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
    serial.setBaudRate(parser.value("speed").toInt());

    // Send data to all network clients
    auto replyToAll = [&](const QByteArray &data)
    {
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
        replyToAll(serial.readAll());
    });

    // Udp Server Events
    QObject::connect(&udpServer, &QUdpSocket::readyRead, [&](){
        while (udpServer.hasPendingDatagrams()) {
            QNetworkDatagram datagram = udpServer.receiveDatagram();
            udpActiveAddress = datagram.senderAddress();
            udpActivePort = quint16(datagram.senderPort());
            broadcast(datagram.data());
        }
    });

    // Tcp Server Events
    QObject::connect(&tcpServer, &QTcpServer::newConnection, [&](){
        while (tcpServer.hasPendingConnections())
        {
            QTcpSocket *client = tcpServer.nextPendingConnection();
            QObject::connect(client, &QTcpSocket::disconnected, [&, client](){
                tcpClientList.removeAll(client);
            });
            QObject::connect(client, &QTcpSocket::readyRead, [&, client](){
                broadcast(client->readAll());
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
        qCritical().noquote().nospace()
            << "Can't open port " << device
            << " : " << serial.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("udp-port") && !udpServer.bind(address, udpPort))
    {
        qCritical().noquote().nospace()
            << "Can't grab UDP "
            << address.toString() << ":" << udpPort
            << " : "  << udpServer.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("tcp-port") && !tcpServer.listen(address, tcpPort))
    {
        qCritical().noquote().nospace()
            << "Can't grab TCP "
            << address.toString() << ":" << tcpPort
            << " : "  << tcpServer.errorString();

        return EXIT_FAILURE;
    }

    if (parser.isSet("ws-port") && !wsServer.listen(address, wsPort))
    {
        qCritical().noquote().nospace()
            << "Can't grab WebSocket "
            << address.toString() << ":" << wsPort
            << " : "  << wsServer.errorString();

        return EXIT_FAILURE;
    }


    return a.exec();
}
