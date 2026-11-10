#include "runtime/vision.hpp"
using namespace okww;using namespace okww::runtime;
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);if(argc!=3)return 2;
    try {
        QJsonArray results;
        for(auto value:readJson(app.arguments()[1])["cases"].toArray()) {
            const auto v=value.toObject();const int w=v["w"].toInt(),h=v["h"].toInt();
            cv::Mat frame(h,w,CV_8UC3);
            const auto point=gamePoint({w,h},v["x"].toDouble(),v["y"].toDouble(),v["hc"].toBool(),v["vc"].toBool());
            const auto box=gameBox(frame,v["x"].toDouble(),v["y"].toDouble(),v["right"].toDouble(),v["bottom"].toDouble(),v["hc"].toBool(),v["vc"].toBool());
            results.append(QJsonArray{point.x(),point.y(),box.x(),box.y(),box.width(),box.height()});
        }
        writeJson(app.arguments()[2],{{"results",results}});return 0;
    }catch(...){return 1;}
}
