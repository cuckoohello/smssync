/******************************************************************************
**
** This file is part of libcommhistory.
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Alexander Shalamov <alexander.shalamov@nokia.com>
**
** This library is free software; you can redistribute it and/or modify it
** under the terms of the GNU Lesser General Public License version 2.1 as
** published by the Free Software Foundation.
**
** This library is distributed in the hope that it will be useful, but
** WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
** or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
** License for more details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this library; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
**
******************************************************************************/

#ifndef COMMHISTORY_SYNCMESSAGEMODEL_H
#define COMMHISTORY_SYNCMESSAGEMODEL_H

#include <QDateTime>
#include <CommHistory/EventModel>
#include <CommHistory/Event>
#include <CommHistory/libcommhistoryexport.h>
using namespace CommHistory;

struct  SyncMessageFilter
{
    int parentId;
    QDateTime time;
    bool lastModified;
    Event::EventType type;
    QString account;

    SyncMessageFilter(int _parentId = INBOX, Event::EventType _type = Event::SMSEvent , QString _account = QString("ring/tel/ring") , QDateTime _time= QDateTime(), bool _lastModified = false)
        :parentId(_parentId),
          type(_type),
          account(_account),
          time(_time),
          lastModified(_lastModified)
    {
    }

};

class SyncMessageModelPrivate;
/*!
 * \class SyncSMSModel
 *  Model for syncing all the stored sms. Initialization of model is done with getEvents
 */
class  SyncMessageModel : public EventModel
{
public:


    /*!
     * Model constructor.
     *
     * \param parent Parent object.
     */
    SyncMessageModel(int parentId = INBOX, Event::EventType  _type = Event::SMSEvent, QString _account = QString("ring/tel/ring"),QDateTime time= QDateTime(), bool lastModified = false, QObject *parent = 0);

    /*!
     * Destructor.
     */
    ~SyncMessageModel();


    /*!
     * Reset model and fetch sms events. Messages are fetched based on SyncSMSFilter
     * This method is used to retrieve the sms present in device during sync session
     * \return true if successful, otherwise false
     */
    bool getEvents();

    /*!
      * if filter.parentId is set, then all messages whose parentId matches that of the filter would be fetched. If parentId is 'ALL', then all messages would be fetched, no constraint would be set in this case
      * If filter.time is set and lastModified and deleted are not set, then all messages whose sent/received time is greater or equal to filter.time would be fetched
      * If filter.time is set and either of lastModified or deleted are  set, then all messages whose sent/received time is lesser than  filter.time and lastModifiedTime is greater or equal to filter.time would be fetched
      */
    void setSyncMessageFilter(const SyncMessageFilter& filter);

private:
    Q_DECLARE_PRIVATE(SyncMessageModel);
};

#endif // SYNCSMSMODEL_H
