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
const char message_header_format[] = "%1 with %2";
const char sync_date_format[] = "yyyy-MM-dd-hh:mm:ss";

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
            arg(time.toUTC().toString("yyyy-MM-dd-hh-mm-ss-zzz")).arg(address).arg(type);
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
   // QScopedPointer<QApplication> app(createApplication(argc, argv));
    QCoreApplication app(argc, argv);


    char message[8192]; // This should be large enough for messages
    if(sms_imap_config())
    {
        qDebug() << "Config error!";
        return 1;
    }
    QString myEmail = QString().fromAscii(accountEmail);
    QString myName = myEmail.split("@").at(0);
    QString timeZone = getTimeZone();

    QContactManager m_contactManager("tracker");
    QHash<QString,struct SMSSyncContact> contactPool;

    channel_conf_t *channel;
    bool isFirstSync;

    if(sms_imap_init())
    {
        qDebug() << "Config error or network error";
        return 1;
    }

    for(channel=channels;channel;channel=channel->next)
    {
        if(channel->sync_time && *channel->sync_time)
            isFirstSync = false;
        else
            isFirstSync = true;
        qDebug() << "Channel "<<channel->name;
        Event::EventType eventType;
        if(!strcasecmp( channel->type, "SMS"))
            eventType = Event::SMSEvent;
        else if (!strcasecmp( channel->type, "IM"))
            eventType = Event::IMEvent;
        else if (!strcasecmp( channel->type, "CALL"))
            eventType = Event::CallEvent;
        else
        {
            qDebug() << "Wrong type for channel "<<channel->name<<"!";
            qDebug() <<"Only SMS/IM/CALL is supported!";
            continue;
        }

        sms_imap_select_mailbox(channel->mail_box);

        SyncMessageModel syncModel(ALL,eventType,channel->account,
                                   isFirstSync ? QDateTime():QDateTime().fromString(QString(channel->sync_time),sync_date_format));

        syncModel.setQueryMode(EventModel::SyncQuery);
        syncModel.getEvents();
        int total = isFirstSync ? syncModel.rowCount() :  syncModel.rowCount() -1;
        if(total < 0)
            total = 0;
        qDebug() << "Total " << total <<" messages need to sync!";

        for (int i= isFirstSync ? 0 :1 ;i < syncModel.rowCount();i++)
        {

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
                    contact.email = QString(number).append("@unknown.email");
                }
                else
                {
                    contact.name = ((QContactDisplayLabel)contacts.first().detail<QContactDisplayLabel>()).label();
                    if(contact.name.isEmpty())
                        contact.name = number;
                    contact.email = ((QContactEmailAddress)contacts.first().detail<QContactEmailAddress>()).emailAddress();
                    if(contact.email.isEmpty())
                        contact.email = QString(number).append("@unknown.email");

                }
                contactPool.insert(number,contact);
            }

            imap_create_header(message,QString(message_header_format).arg(channel->label).arg(contact.name).toUtf8().data());

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
                              arg(stores->prefrence).arg(syncModel.data(syncModel.index(i,EventModel::GroupId),0).toInt()).toUtf8().data());

            imap_add_header(message,"X-smssync-id",syncModel.data(syncModel.index(i,EventModel::EventId),0).toString().toUtf8().data());
            imap_add_header(message,"X-smssync-address",number.toUtf8().data());
            imap_add_header(message,"X-smssync-datatype",channel->label);


            imap_add_header(message,"X-smssync-backup-time",QDateTime::currentDateTime().toLocalTime().
                            toString("ddd, d MMM yyyy H:m:s ").append(timeZone).toUtf8().data());

            if(eventType != Event::CallEvent)
                imap_add_contect(message,syncModel.data(syncModel.index(i,EventModel::FreeText),0).toString().toUtf8().data());
            else
            {
                QString content;
                if ((direction == Event::Outbound)  || !syncModel.data(syncModel.index(i,EventModel::IsMissedCall)).toBool())
                {
                    QDateTime start = syncModel.data(syncModel.index(i,EventModel::StartTime),0).toDateTime();
                    QDateTime end = syncModel.data(syncModel.index(i,EventModel::EndTime),0).toDateTime();
                    int seconds = start.secsTo(end);
                    if(seconds<0)
                        seconds = -seconds;
                    int mins = seconds/60;
                    int secs = seconds%60;
                    int hours = mins/60;
                    mins %= 60;
                    content = content.sprintf("%ds(%02d:%02d:%02d)\n",seconds,hours,mins,secs);
                }
                if(direction == Event::Outbound)
                    content.append(number).append("(Outgoing Call)");
                else if(syncModel.data(syncModel.index(i,EventModel::IsMissedCall)).toBool())
                    content.append(number).append("(Missed Call)");
                else
                    content.append(number).append("(Incoming Call)");
                imap_add_contect(message,content.toUtf8().data());
            }

            if(sms_imap_sync_one(message))
            {
                /* sync error */
                qDebug() << "Sync network error!";
                char *date = syncModel.data(syncModel.index(i,EventModel::EndTime),0).toDateTime()
                .toLocalTime().toString(sync_date_format).toUtf8().data();
                memcpy(channel->sync_time,date,strlen(date));
                channel->sync_time[strlen(date)] = 0;
                save_state_config(0,0);
                break;
            }
            if(syncModel.rowCount()-i <= 10 || i%10 == (isFirstSync ? 0 : 1))
                qDebug() << (isFirstSync ? i : i-1) << "/" <<total <<" synced!";

            if(i%10 == (isFirstSync ? 0 : 1))
            {
                /* backup status every 10 backups */
                char *date = syncModel.data(syncModel.index(i,EventModel::EndTime),0).toDateTime()
                .toLocalTime().toString(sync_date_format).toUtf8().data();
                memcpy(channel->sync_time,date,strlen(date));
                channel->sync_time[strlen(date)] = 0;
                save_state_config(0,1);
            }
            if(i == syncModel.rowCount()-1)
            {
                char *date = syncModel.data(syncModel.index(i,EventModel::EndTime),0).toDateTime()
                .toLocalTime().toString(sync_date_format).toUtf8().data();
                memcpy(channel->sync_time,date,strlen(date));
                channel->sync_time[strlen(date)] = 0;
                save_state_config(0,1);
            }
        }
    }

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
