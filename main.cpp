#include <QtGui/QApplication>
#include <QDebug>
#include <QLocale>
#include <QContactManager>
#include <QContactPhoneNumber>
#include <QContact>
#include <QContactEmailAddress>
#include <QContactOnlineAccount>
#include <QContactDetailFilter>
#include <QFile>
#include "isync.h"
#include "qmlapplicationviewer.h"
#include "base64.h"
#include "syncmessagemodel.h"

using namespace CommHistory;
QTM_USE_NAMESPACE;

const char refrence_format[] = "%1.%2@n9-sms-backup-local";
const char sms_header[] = "SMS with %1";
const char im_header[] = "Fetion with %1";
const char call_header[] = "Call with %1";
const char table[] = "0123456789abcdefghijklmnopqrstuvwxyz";
const char sync_date_format[] = "dd.MM.yyyy hh:mm:ss";

struct SMSSyncContact{
    QString name;
    QString email;
};

QString getTimeZone()
{
    QDateTime dt1 = QDateTime::currentDateTime();
    QDateTime dt2 = dt1.toUTC();
    dt1.setTimeSpec(Qt::UTC);

    int offset = dt2.secsTo(dt1)/60;
    if(offset <0)
    {
        offset = -offset;
        return QString().sprintf("-%d%02d",offset/60,offset%60);
    }else
    {
        return QString().sprintf("+%d%02d",offset/60,offset%60);
    }
 }

QString createMessageId(QDateTime time,QString address,int type)
{
    return QString("%1-%2-%3").
            arg(time.toUTC().toString("dd-MM-yyyy-hh-mm-ss-zzz")).arg(address).arg(type);
}

QContactFilter IMAccountFilter(const QString &id)
{
    QContactDetailFilter l;
    l.setDetailDefinitionName(QContactOnlineAccount::DefinitionName, QContactOnlineAccount::FieldAccountUri);
    l.setValue(id);
    l.setMatchFlags(QContactFilter::MatchExactly);
    return l;
}

Q_DECL_EXPORT int main(int argc, char *argv[])
{
   // QLocale::setDefault(QLocale(QLocale::English,QLocale::UnitedStates));
    QScopedPointer<QApplication> app(createApplication(argc, argv));

    char message[8192]; // This should be large enough for messages
    if(sms_imap_prepare())
    {
        qDebug() << "Config error or network error!";
        return 1;
    }
    QString myEmail = QString().fromAscii(accountEmail);
    QString myName = myEmail.split("@").at(0);
    QString timeZone = getTimeZone();
    bool isFirst = false;
    if(load_state_config(0,0))
    {
        qDebug() << "First sync";
        srandom(time(NULL));
        for(int i=0 ;i <24;i++)
            prefrence[i] = table[rand()%strlen(table)];
        prefrence[24] = 0;

        isFirst = true;
    }

    QContactManager m_contactManager("tracker");
    QHash<QString,struct SMSSyncContact> contactPool;

    SyncMessageModel syncModel(ALL,isFirst ? QDateTime():QDateTime().fromString(QString(sync_date),sync_date_format));

    syncModel.setQueryMode(EventModel::SyncQuery);
    syncModel.getEvents();
    int total = isFirst ? syncModel.rowCount() :  syncModel.rowCount() -1;
    qDebug() << "Total " << total <<" messages need to sync!";

    /* no new messages ,so just return 0 */
    if(isFirst && syncModel.rowCount() == 0)
        goto sync_done;
    else if (!isFirst && syncModel.rowCount() == 1)
        goto sync_done;

    for (int i= isFirst ? 0 :1 ;i < syncModel.rowCount();i++)
    {
        int eventType = syncModel.data(syncModel.index(i,EventModel::EventType),0).toInt();

        memset(message,0,8192);

        QString number = syncModel.data(syncModel.index(i,EventModel::RemoteUid),0).toString();
        int direction = syncModel.data(syncModel.index(i,EventModel::Direction),0).toInt();
        SMSSyncContact contact = contactPool.value(number);
        if(contact.name.isEmpty())
        {
            QList<QContact> contacts = m_contactManager.contacts(
                        (eventType == Event::IMEvent) ? IMAccountFilter(number):QContactPhoneNumber::match(number));
            if (contacts.isEmpty())
            {
                contact.name = number;
                contact.email = number.append("@unknown.email");
            }
            else
            {
                contact.name = ((QContactDisplayLabel)contacts.first().detail<QContactDisplayLabel>()).label();
                if(contact.name.isEmpty())
                    contact.name = number;
                contact.email = ((QContactEmailAddress)contacts.first().detail<QContactEmailAddress>()).emailAddress();
                if(contact.email.isEmpty())
                    contact.email = number.append("@unknown.email");

            }
            contactPool.insert(number,contact);
        }
        switch(eventType)
        {
        case Event::SMSEvent:
            imap_create_header(message, QString(sms_header).arg(contact.name).toUtf8().data());
            break;
        case Event::IMEvent:
            imap_create_header(message, QString(im_header).arg(contact.name).toUtf8().data());
            break;
        case Event::CallEvent:
            imap_create_header(message, QString(call_header).arg(contact.name).toUtf8().data());
            break;
        default:
            qDebug() << i << " unkown Event type "<<eventType;

        }

        if (direction == Event::Inbound)
        {
            imap_add_address(message,"From",contact.name.toUtf8().data(),contact.email.toUtf8().data());
            imap_add_address(message,"To",myName.toUtf8().data(),myEmail.toUtf8().data());
        }else
        {
            imap_add_address(message,"From",myName.toUtf8().data(),myEmail.toUtf8().data());
            imap_add_address(message,"To",contact.name.toUtf8().data(),contact.email.toUtf8().data());
        }

        imap_add_header(message,"Date",
                        syncModel.data(syncModel.index(i,EventModel::StartTime),0).toDateTime()
                        .toLocalTime().toString("ddd, d MMM yyyy H:m:s ").append(timeZone).toUtf8().data());

        if (eventType == Event::SMSEvent)
            imap_add_identify(message,"Message-ID",
                              syncModel.data(syncModel.index(i,EventModel::MessageToken),0).toString().append("@n9-sms-backup.local").toUtf8().data());
        else
            imap_add_identify(message,"Message-ID",
                              createMessageId(
                                  syncModel.data(syncModel.index(i,EventModel::StartTime),0).toDateTime(),
                                  number,eventType).append("@n9-sms-backup.local").toUtf8().data());

        imap_add_identify(message,"References",QString(refrence_format).
                          arg(prefrence).arg(syncModel.data(syncModel.index(i,EventModel::GroupId),0).toInt()).toUtf8().data());

        imap_add_header(message,"X-smssync-id",syncModel.data(syncModel.index(i,EventModel::EventId),0).toString().toUtf8().data());
        imap_add_header(message,"X-smssync-address",number.toUtf8().data());
        switch(eventType)
        {
        case Event::SMSEvent:
            imap_add_header(message,"X-smssync-datatype","SMS");
            break;
        case Event::IMEvent:
            imap_add_header(message,"X-smssync-datatype","FETION");
            break;
        case Event::CallEvent:
            imap_add_header(message,"X-smssync-datatype","CALLLOG");
            break;
        default:
            qDebug() << i << " unkown Event type "<<eventType;
        }

        imap_add_header(message,"X-smssync-backup-time",QDateTime::currentDateTime().toLocalTime().
                        toString("ddd, d MMM yyyy H:m:s ").append(timeZone).toUtf8().data());

        imap_add_contect(message,syncModel.data(syncModel.index(i,EventModel::FreeText),0).toString().toUtf8().data());

        if(sms_imap_sync_one(message,(eventType == Event::CallEvent) ? "Call log" : "SMS"))
        {
            /* sync error */
            qDebug() << "Sync network error!";
            char *date = syncModel.data(syncModel.index(i-1,3),0).toDateTime()
            .toLocalTime().toString(sync_date_format).toUtf8().data();
            memcpy(sync_date,date,strlen(date));
            sync_date[strlen(date)] = 0;
            save_state_config(0,0);
            goto sync_done;
        }
        if(syncModel.rowCount()-i <= 10 || i%10 == 0)
            qDebug() << (isFirst ? i : i-1) << "/" <<total <<" synced!";

        if(i%10 == 0)
        {
            /* backup status every 10 backups */
            char *date = syncModel.data(syncModel.index(i,3),0).toDateTime()
            .toLocalTime().toString(sync_date_format).toUtf8().data();
            memcpy(sync_date,date,strlen(date));
            sync_date[strlen(date)] = 0;
            save_state_config(0,1);
        }

    }


    {/* date should set to recent sync message if cancel is supported*/
        char *date = syncModel.data(syncModel.index(syncModel.rowCount()-1,3),0).toDateTime()
                .toLocalTime().toString(sync_date_format).toUtf8().data();
        memcpy(sync_date,date,strlen(date));
        sync_date[strlen(date)] = 0;
        save_state_config(0,0);
    }

    sync_done:
    sms_imap_close();
    qDebug() << "Sync done";


    /*
    QmlApplicationViewer viewer;
    viewer.setOrientation(QmlApplicationViewer::ScreenOrientationAuto);
    viewer.setMainQmlFile(QLatin1String("qml/smssync/main.qml"));
    viewer.showExpanded();
    */

    return 0;
}
