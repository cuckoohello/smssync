#include <QtGui/QApplication>
#include <QDebug>
#include <QLocale>
#include <QContactManager>
#include <QContactPhoneNumber>
#include <QContact>
#include <QContactEmailAddress>
#include <QFile>
#include "isync.h"
#include "qmlapplicationviewer.h"
#include "base64.h"
#include "syncmessagemodel.h"
using namespace CommHistory;
QTM_USE_NAMESPACE;

Q_DECL_EXPORT int main(int argc, char *argv[])
{
   // QLocale::setDefault(QLocale(QLocale::English,QLocale::UnitedStates));
    QScopedPointer<QApplication> app(createApplication(argc, argv));


    SyncMessageModel syncModel(ALL);

    syncModel.setQueryMode(EventModel::SyncQuery);
    syncModel.getEvents();
    qDebug() << "Total " << syncModel.rowCount() <<" messages need to sync!";

    return 0;
}
