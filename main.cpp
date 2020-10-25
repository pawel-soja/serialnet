#include <QCoreApplication>
#include <QSerialPort>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName("serialnet");
    QCoreApplication::setApplicationVersion("0.1");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::tr("Serial port via udp server"));
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addOptions({
        {{"d", "device"}, QCoreApplication::tr("Device path, default: /dev/serial0"),  QCoreApplication::tr("path"),     "/dev/serial0"},
        {{"b", "baud"},   QCoreApplication::tr("Serial port baudrade, default: 9600"), QCoreApplication::tr("baudrate"), "9600"},
        {{"p", "port"},   QCoreApplication::tr("Listen port number, default: 11880"),  QCoreApplication::tr("number"),   "11880"},
        {{"e", "echo"},   QCoreApplication::tr("Reply request message to client")},
    });
    parser.process(a);
    qDebug() << QCoreApplication::tr("Asd");


    bool echo      = parser.isSet("echo");
    quint16 port   = parser.value("port").toUShort();
    QString device = parser.value("device");

    QSerialPort serial;
    QUdpSocket socket;

    QHostAddress clientAddress;
    quint16      clientPort = 0;

    serial.setPortName(device);
    serial.setBaudRate(parser.value("baud").toInt());

    QObject::connect(&serial, &QSerialPort::readyRead, [&](){
        QByteArray data = serial.readAll();
        if (clientPort != 0)
            socket.writeDatagram(data, clientAddress, clientPort);
    });

    QObject::connect(&socket, &QUdpSocket::readyRead, [&](){
        while (socket.hasPendingDatagrams()) {
            QNetworkDatagram datagram = socket.receiveDatagram();
            clientAddress = datagram.senderAddress();
            clientPort = quint16(datagram.senderPort());
            if (echo)
                socket.writeDatagram(datagram.data(), clientAddress, clientPort);
            serial.write(datagram.data());
        }
    });

    if (!serial.open(QSerialPort::ReadWrite))
    {
        qCritical() << "Cannot open port:" << device;
        return 0;
    }

    if (!socket.bind(QHostAddress::Any, port))
    {
        qCritical() << "Cannot listen on " << port;
        return 0;
    }

    return a.exec();
}
