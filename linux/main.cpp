#include <QtWidgets>
#include <QtNetwork>
#undef signals
#include <atspi/atspi.h>
#include <functional>

// One GUI event loop: idle has no timer, network and child I/O are asynchronous.
class Selection : public QWidget {
    QImage image; QPoint anchor, cursor; bool dragging = false;
QRect region() const { return QRect(qMin(anchor.x(),cursor.x()),qMin(anchor.y(),cursor.y()),qAbs(anchor.x()-cursor.x()),qAbs(anchor.y()-cursor.y())); }
public:
    std::function<void(QByteArray)> done;
    Selection(QImage im, QRect geometry) : QWidget(nullptr, Qt::FramelessWindowHint), image(std::move(im)) {
        setGeometry(geometry); setCursor(Qt::CrossCursor); setMouseTracking(true);
        setWindowTitle("ShotOcr Selection");
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.drawImage(rect(), image); p.fillRect(rect(), QColor(0,0,0,90));
        if (dragging) {
            QRect r = region();
            p.setClipRect(r); p.drawImage(rect(),image); p.setClipping(false);
            p.setPen(QPen(QColor(72,180,255),2)); p.drawRect(r);
        }
    }
    void keyPressEvent(QKeyEvent* e) override { if(e->key()==Qt::Key_Escape) done({}); }
    void mousePressEvent(QMouseEvent* e) override {
        if(e->button()==Qt::RightButton) { done({}); return; }
        if(e->button()==Qt::LeftButton) { anchor=cursor=e->position().toPoint(); dragging=true; update(); }
    }
    void mouseMoveEvent(QMouseEvent* e) override { if(dragging) { cursor=e->position().toPoint(); update(); } }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if(e->button()!=Qt::LeftButton || !dragging) return;
        cursor=e->position().toPoint(); QRect r=region().intersected(rect());
        QByteArray bytes;
        if(r.width()>1 && r.height()>1) {
            double sx=double(image.width())/width(), sy=double(image.height())/height();
            QBuffer b(&bytes); b.open(QIODevice::WriteOnly);
            image.copy(QRect(qRound(r.x()*sx),qRound(r.y()*sy),qRound(r.width()*sx),qRound(r.height()*sy))).save(&b,"PNG");
        }
        done(bytes);
    }
};

class App : public QObject {
    friend struct IntegrationTest;
    enum State { Idle, Capturing, Selecting, Recording, Recognizing, Delivering } state=Idle;
    QLocalServer server; QSystemTrayIcon tray; QMenu menu; QSettings settings;
    QNetworkAccessManager network; QNetworkReply* reply=nullptr;
    QProcess* process=nullptr; Selection* selection=nullptr;
    QTimer limit; QByteArray audio; quint64 generation=0;
    AtspiAccessible *focused=nullptr, *target=nullptr; AtspiEventListener* listener=nullptr;
    bool submitted=false;
    bool english=false; QJsonValue windowId;
    void tip(QString s) { tray.setToolTip("ShotOcr · "+s); tray.showMessage("ShotOcr",s,QSystemTrayIcon::Information,2200); }
    static void focusEvent(AtspiEvent* e, void* data) {
        auto a=static_cast<App*>(data);
        if(e->detail1) { if(a->focused) g_object_unref(a->focused); a->focused=ATSPI_ACCESSIBLE(g_object_ref(e->source)); }
        else if(a->focused==e->source) { g_object_unref(a->focused); a->focused=nullptr; }
        g_boxed_free(ATSPI_TYPE_EVENT,e);
    }
    // Small IPC queries; no niri subprocess or shell quoting.
    void niri(QJsonValue request, std::function<void(QJsonValue)> callback) {
        auto s=new QLocalSocket(this); auto data=std::make_shared<QByteArray>();
        connect(s,&QLocalSocket::connected,s,[s,request] {
            QJsonArray wrapper{request}; QByteArray b=QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
            s->write(b.mid(1,b.size()-2)+"\n");
        });
        auto completed=std::make_shared<bool>(false);
        auto finish=[s,callback,completed](QJsonValue v) { if(*completed)return; *completed=true; callback(v); s->deleteLater(); };
        connect(s,&QLocalSocket::readyRead,s,[s,data,finish] {
            *data+=s->readAll(); if(!data->contains('\n'))return;
            auto o=QJsonDocument::fromJson(*data).object(); finish(o["Ok"]);
        });
        connect(s,&QLocalSocket::errorOccurred,s,[finish](auto){finish({});});
        QTimer::singleShot(1500,s,[finish]{finish({});});
        s->connectToServer(qEnvironmentVariable("NIRI_SOCKET"));
    }
    void remember(std::function<void()> next) {
        if(target)g_object_unref(target);
        target=focused ? ATSPI_ACCESSIBLE(g_object_ref(focused)) : nullptr;
        auto id=generation;
        niri("FocusedWindow",[this,id,next](QJsonValue v){if(id!=generation)return;windowId=v["FocusedWindow"]["id"];next();});
    }
    void run(QString cmd, QStringList args, std::function<void(int,QByteArray,QString)> done, QByteArray input={}) {
        auto p=new QProcess(this); process=p; auto id=generation;
        auto called=std::make_shared<bool>(false);
        auto finish=[this,p,id,done,called](int code) {
            if(*called)return;
            *called=true;
            if(process==p) process=nullptr;
            auto out=p->readAllStandardOutput(); auto error=QString::fromUtf8(p->readAllStandardError());
            if(error.isEmpty() && code)error=p->errorString();
            p->deleteLater(); if(id==generation)done(code,out,error);
        };
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),p,[finish](int code,auto status){finish(status==QProcess::NormalExit?code:-1);});
        connect(p,&QProcess::errorOccurred,p,[finish](auto error){if(error==QProcess::FailedToStart)finish(-1);});
        p->start(cmd,args); if(!input.isEmpty())p->write(input); p->closeWriteChannel();
    }
    void reset() {
        limit.stop(); state=Idle; audio.clear();
        if(target){g_object_unref(target);target=nullptr;}
    }
    void fail(QString error) { reset();tip(error); }
    void cancel() {
        if(state==Idle)return;
        ++generation;
        if(reply){auto r=reply;reply=nullptr;r->abort();}
        if(process){process->kill();process=nullptr;}
        if(selection){selection->hide();selection->deleteLater();selection=nullptr;}
        reset();tip("已取消");
    }
    void capture() {
        if(state!=Idle)return;
        state=Capturing; ++generation;
        remember([this] {
            run("grim",{"-t","ppm","-"},[this](int code,QByteArray bytes,QString error) {
                if(code){fail("截图失败："+error);return;}
                QImage image=QImage::fromData(bytes,"PPM");
                if(image.isNull()){fail("截图图像无效");return;}
                QRect desktop; for(auto s:QGuiApplication::screens())desktop=desktop.united(s->geometry());
                selection=new Selection(std::move(image),desktop); state=Selecting;
                selection->done=[this](QByteArray png) {
                    selection->hide();selection->deleteLater();selection=nullptr;
                    // The compositor restores the previous focus after unmapping the overlay.
                    if(png.isEmpty()){cancel();return;} recognize(png,false);
                };
                selection->showFullScreen();selection->activateWindow();
            });
        });
    }
    static QByteArray wav(QByteArray pcm) {
        pcm.truncate(pcm.size() & ~1); QByteArray out;
        auto le=[&out](quint32 n,int count){while(count--){out.append(char(n));n>>=8;}};
        out+="RIFF";le(36+pcm.size(),4);out+="WAVEfmt ";le(16,4);le(1,2);le(1,2);
        le(16000,4);le(32000,4);le(2,2);le(16,2);out+="data";le(pcm.size(),4);out+=pcm;return out;
    }
    void voice() {
        if(state==Recording){ submitted=true;if(process)process->terminate();limit.stop();return; }
        if(state!=Idle)return;
        state=Recording;++generation;
        remember([this] {
            audio.clear();submitted=false;
            run("pw-record",{"--raw","--rate","16000","--channels","1","--format","s16","--sample-count","960000","-"},
                [this](int code,QByteArray tail,QString error) {
                    limit.stop();audio+=tail;
                    if(audio.isEmpty() || (code && !submitted)){fail("录音失败："+error);return;}
                    // SIGTERM is how a user submits an unfinished raw stream.
                    Q_UNUSED(code);
                    auto data=wav(audio);audio.clear();recognize(data,true);
                });
            auto p=process;auto id=generation;
            connect(p,&QProcess::readyReadStandardOutput,p,[this,p,id]{if(id==generation)audio+=p->readAllStandardOutput();});
            limit.start(60000);tip(english?"录音中 · English · 再按 Ctrl+Shift+H 提交":"录音中 · 中文 · 再按 Ctrl+Shift+H 提交");
        });
    }
    void recognize(QByteArray data,bool voice) {
        state=Recognizing;tip("正在识别 · Ctrl+Shift+Esc 取消");
        QUrl url("https://aidemo.youdao.com"+(voice?QString("/asr?lang=")+(english?"en":"zh-CHS"):QString("/ocrapi1")));
        QNetworkRequest req(url);req.setTransferTimeout(30000);req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        QByteArray body;
        if(voice) {
            req.setHeader(QNetworkRequest::ContentTypeHeader,"multipart/form-data; boundary=----ShotOcrWavBoundary");
            body="------ShotOcrWavBoundary\r\nContent-Disposition: form-data; name=\"audioData\"; filename=\"record.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
            body+=data;body+="\r\n------ShotOcrWavBoundary--\r\n";
        } else {
            req.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded; charset=UTF-8");
            body="imgBase="+QUrl::toPercentEncoding("data:image/png;base64,"+QString::fromLatin1(data.toBase64()))+"&lang=&company=";
        }
        auto r=network.post(req,body);reply=r;auto id=generation;
        connect(r,&QNetworkReply::readyRead,r,[r]{if(r->bytesAvailable()>2*1024*1024)r->abort();});
        connect(r,&QNetworkReply::finished,this,[this,r,id,voice] {
            r->deleteLater();if(id!=generation)return;reply=nullptr;
            if(r->error()!=QNetworkReply::NoError || r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){fail("识别请求失败："+r->errorString());return;}
            QJsonParseError error;auto doc=QJsonDocument::fromJson(r->readAll(),&error);auto obj=doc.object();
            if(error.error!=QJsonParseError::NoError || !doc.isObject()){fail("识别响应无效");return;}
            if(obj.contains("errorCode") && obj["errorCode"].toVariant().toString()!="0"){fail("有道错误："+obj["errorCode"].toVariant().toString());return;}
            QStringList lines;
            if(voice && obj["result"].isString())lines<<obj["result"].toString();
            else for(auto v:obj[voice?"result":"lines"].toArray()) {
                QJsonValue text=voice?QJsonValue(v):v.toObject()["words"];
                if(!text.isString()){fail("识别文本格式无效");return;}lines<<text.toString();
            }
            QString text=lines.join('\n');
            if(text.isEmpty() || text.contains(QChar(0))){fail("没有识别到文字");return;}
            deliver(text);
        });
    }
    void deliver(QString text) {
        state=Delivering;
        run("wl-copy",{"--type","text/plain;charset=utf-8"},[this](int code,QByteArray,QString error) {
            if(code){fail("剪贴板写入失败："+error);return;}
            auto id=generation;
            niri("FocusedWindow",[this,id](QJsonValue v) {
                if(id!=generation)return;
                bool pasted=false;
                if(target && target==focused && windowId.isDouble() && v["FocusedWindow"]["id"]==windowId) {
                    auto states=atspi_accessible_get_state_set(target);
                    if(atspi_state_set_contains(states,ATSPI_STATE_EDITABLE) && atspi_state_set_contains(states,ATSPI_STATE_FOCUSED)) {
                        auto edit=atspi_accessible_get_editable_text_iface(target);
                        auto text=atspi_accessible_get_text_iface(target);
                        GError* error=nullptr;
                        if(edit && text) {
                            int offset=atspi_text_get_caret_offset(text,&error);
                            if(!error && offset>=0)pasted=atspi_editable_text_paste_text(edit,offset,&error);
                        }
                        if(error)g_error_free(error);
                        if(edit)g_object_unref(edit);
                        if(text)g_object_unref(text);
                    }
                    g_object_unref(states);
                }
                reset();tip(pasted?"已复制并粘贴":"已复制");
            });
        },text.toUtf8());
    }
public:
    App(QString socket) : settings("ShotOcr","ShotOcr") {
        english=settings.value("english",false).toBool();
        tray.setIcon(QIcon::fromTheme("edit-find"));tray.setToolTip("ShotOcr");
        menu.addAction("截图 · Ctrl+Shift+S",this,[this]{capture();});
        menu.addAction("录音 / 提交 · Ctrl+Shift+H",this,[this]{voice();});
        menu.addAction("取消 · Ctrl+Shift+Esc",this,[this]{cancel();});menu.addSeparator();
        auto group=new QActionGroup(this);
        for(bool en:{false,true}) {
            auto action=menu.addAction(en?"English":"中文");action->setCheckable(true);action->setChecked(en==english);group->addAction(action);
            connect(action,&QAction::triggered,this,[this,en]{english=en;settings.setValue("english",en);});
        }
        menu.addSeparator();menu.addAction("退出",this,[this]{cancel();qApp->quit();});
        tray.setContextMenu(&menu);tray.show();
        limit.setSingleShot(true);connect(&limit,&QTimer::timeout,this,[this]{submitted=true;if(process)process->terminate();});
        atspi_init();atspi_set_timeout(500,500);
        listener=atspi_event_listener_new(focusEvent,this,nullptr);
        atspi_event_listener_register(listener,"object:state-changed:focused",nullptr);
        connect(&server,&QLocalServer::newConnection,this,[this] {
            while(auto s=server.nextPendingConnection()) {
                connect(s,&QLocalSocket::readyRead,s,[this,s]{
                    if(!s->canReadLine())return;
                    auto cmd=s->readLine().trimmed();
                    command(cmd);s->disconnectFromServer();
                });connect(s,&QLocalSocket::disconnected,s,&QObject::deleteLater);
            }
        });
        if(!server.listen(socket))qFatal("Cannot listen on ShotOcr socket");
    }
    ~App() {
        atspi_event_listener_deregister(listener,"object:state-changed:focused",nullptr);
        g_object_unref(listener);if(focused)g_object_unref(focused);if(target)g_object_unref(target);atspi_exit();
    }
    void command(QByteArray cmd) { if(cmd=="capture")capture();else if(cmd=="voice")voice();else if(cmd=="cancel")cancel();else if(cmd=="quit"){cancel();qApp->quit();} }
};
int main(int argc,char** argv) {
    QString socket=QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)+"/shotocr.sock";
    QLockFile lock(socket+".lock");
    QByteArray cmd=argc>1?QByteArray(argv[1]):QByteArray();
    if(!lock.tryLock(0)) {
        QCoreApplication client(argc,argv);
        QLocalSocket s;s.connectToServer(socket);
        if(!s.waitForConnected(1500)){qCritical("ShotOcr is not responding");return 1;}
        s.write(cmd+"\n");s.waitForBytesWritten(1500);return 0;
    }
    QApplication app(argc,argv);app.setApplicationName("ShotOcr");app.setQuitOnLastWindowClosed(false);
    QLocalServer::removeServer(socket);
    App host(socket);if(!cmd.isEmpty())QTimer::singleShot(0,&host,[&host,cmd]{host.command(cmd);});
    return app.exec();
}
