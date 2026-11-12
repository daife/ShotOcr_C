#define main shotocr_entry
#include "main.cpp"
#undef main
#include <iostream>

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QImage image(400,200,QImage::Format_RGB32); image.fill(Qt::red);
    Selection selection(image,QRect(0,0,200,100));
    QByteArray result;int calls=0;
    selection.done=[&](QByteArray b){result=b;++calls;};
    auto mouse=[&](QEvent::Type type,QPointF pos,Qt::MouseButton button,Qt::MouseButtons buttons) {
        QMouseEvent event(type,pos,pos,button,buttons,Qt::NoModifier);
        QApplication::sendEvent(&selection,&event);
    };
    // Reverse drag at a 2x scale: a 50x30 logical selection maps to 100x60 pixels.
    mouse(QEvent::MouseButtonPress,{80,60},Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,{30,30},Qt::LeftButton,Qt::NoButton);
    auto cropped=QImage::fromData(result,"PNG");
    if(calls!=1 || cropped.size()!=QSize(100,60) || cropped.pixelColor(0,0)!=QColor(Qt::red))return 1;
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);
    QApplication::sendEvent(&selection,&escape);
    if(calls!=2 || !result.isEmpty())return 2;
    mouse(QEvent::MouseButtonPress,{20,20},Qt::RightButton,Qt::RightButton);
    if(calls!=3 || !result.isEmpty())return 3;
    mouse(QEvent::MouseButtonPress,{5,5},Qt::LeftButton,Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease,{5,5},Qt::LeftButton,Qt::NoButton);
    if(calls!=4 || !result.isEmpty())return 4;
    std::cout<<"scaled reverse drag, Esc, right click and empty selection passed\n";
}
