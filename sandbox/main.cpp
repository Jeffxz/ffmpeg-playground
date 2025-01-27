#include <QCoreApplication>
#include <QDebug>
extern "C"
{
    #include <libavformat/avformat.h>
    #include <libavutil/dict.h>
}

int main(int argc, char **argv)
{
  QCoreApplication app (argc, argv);

  qDebug() << avformat_version();

  return 0;
}