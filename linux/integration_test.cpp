// Explicit live integration check: sends only generated text to Youdao and
// replaces the clipboard. Not part of the offline CTest suite.
#define main shotocr_entry
#include "main.cpp"
#undef main
struct IntegrationTest {
    static int run(QApplication& app) {
        App host(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/shotocr-test.sock");
        QImage image(700,130,QImage::Format_RGB32);image.fill(Qt::white);
        QPainter p(&image);p.setPen(Qt::black);p.setFont(QFont("sans",32));p.drawText(20,80,"ShotOcr TEST 123");p.end();
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
        ++host.generation;host.recognize(png,false);
        bool timedOut=false;QTimer timeout;timeout.setSingleShot(true);
        QObject::connect(&timeout,&QTimer::timeout,&app,[&]{timedOut=true;app.quit();});timeout.start(35000);
        QTimer check;QObject::connect(&check,&QTimer::timeout,&app,[&]{if(host.state==App::Idle)app.quit();});check.start(50);
        app.exec();if(timedOut)return 1;
        QProcess paste;paste.start("wl-paste",{"--no-newline"});
        if(!paste.waitForFinished(2000) || paste.readAllStandardOutput().trimmed()!="ShotOcr TEST 123")return 2;
        host.recognize(png,false);host.cancel();
        if(host.reply || host.state!=App::Idle)return 3;
        return 0;
    }
};
int main(int argc,char** argv) { QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);return IntegrationTest::run(app); }
