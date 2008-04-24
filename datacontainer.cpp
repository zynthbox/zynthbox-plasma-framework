/*
 *   Copyright 2006-2007 Aaron Seigo <aseigo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "datacontainer.h"

#include <QVariant>

#include <KDebug>

#include "plasma.h"
#include "datacontainer_p.h"

namespace Plasma
{

DataContainer::DataContainer(QObject* parent)
    : QObject(parent),
      d(new Private())
{
}

DataContainer::~DataContainer()
{
    delete d;
}

const DataEngine::Data DataContainer::data() const
{
    return d->data;
}

void DataContainer::setData(const QString& key, const QVariant& value)
{
    if (value.isNull() || !value.isValid()) {
        d->data.remove(key);
    } else {
        d->data[key] = value;
    }

    d->dirty = true;
    d->updateTs.start();
}

void DataContainer::removeAllData()
{
    if (d->data.count() < 1) {
        // avoid an update if we don't have any data anyways
        return;
    }

    d->data.clear();
    d->dirty = true;
    d->updateTs.start();
}

void DataContainer::checkForUpdate()
{
    if (d->dirty) {
        emit dataUpdated(objectName(), d->data);

        foreach (SignalRelay* relay, d->relays) {
            relay->checkQueueing();
        }

        d->dirty = false;
    }
}

int DataContainer::timeSinceLastUpdate() const
{
    //FIXME: we still assume it's been <24h
    //and ignore possible daylight savings changes
    return d->updateTs.elapsed();
}

bool DataContainer::hasUpdates() const
{
    if (d->cached) {
        //some signalrelay needs us to pretend we did an update
        d->cached = false;
        return true;
    }
    return d->dirty;
}

void DataContainer::setNeedsUpdate(bool update)
{
    d->cached = update;
}

bool DataContainer::visualizationIsConnected(QObject *visualization) const
{
    return d->relayObjects.find(visualization) != d->relayObjects.constEnd();
}

void DataContainer::checkUsage()
{
    if (d->relays.count() < 1 &&
        receivers(SIGNAL(dataUpdated(QString, Plasma::DataEngine::Data))) < 1) {
        // DO NOT CALL ANYTHING AFTER THIS LINE AS IT MAY GET DELETED!
        emit becameUnused(objectName());
    }
}

void DataContainer::connectVisualization(QObject* visualization, uint pollingInterval, Plasma::IntervalAlignment alignment)
{
    //kDebug() << "connecting visualization" << visualization << "at interval of" << pollingInterval;
    QMap<QObject *, SignalRelay *>::iterator objIt = d->relayObjects.find(visualization);
    bool connected = objIt != d->relayObjects.end();
    if (connected) {
        // this visualization is already connected. just adjust the update
        // frequency if necessary
        SignalRelay *relay = objIt.value();
        if (relay) {
            // connected to a relay
            //kDebug() << "     already connected, but to a relay";

            if (relay->receiverCount() == 1) {
                //kDebug() << "    removing relay, as it is now unused";
                d->relays.remove(relay->m_interval);
                delete relay;
            } else {
                disconnect(relay, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                           visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
                //relay->isUnused();
            }
        } else if (pollingInterval < 1) {
            // the visualization was connected already, but not to a relay
            // and it still doesn't want to connect to a relay, so we have
            // nothing to do!
            //kDebug() << "     already connected, nothing to do";
            return;
        } else {
            disconnect(this, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                       visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
        }
    } else {
        connect(visualization, SIGNAL(destroyed(QObject*)),
                this, SLOT(disconnectVisualization(QObject*)));//, Qt::QueuedConnection);
    }


    if (pollingInterval < 1) {
//        kDebug() << "    connecting directly";
        d->relayObjects[visualization] = 0;
        connect(this, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
    } else {
//        kDebug() << "    connecting to a relay";
        SignalRelay *relay = d->signalRelay(this, visualization, pollingInterval, alignment);
        connect(relay, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
    }
}

void DataContainer::disconnectVisualization(QObject* visualization)
{
    QMap<QObject *, SignalRelay *>::iterator objIt = d->relayObjects.find(visualization);

    if (objIt == d->relayObjects.end() || !objIt.value()) {
        // it is connected directly to the DataContainer itself
        disconnect(this, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                   visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
    } else {
        SignalRelay *relay = objIt.value();

        if (relay->receiverCount() == 1) {
            d->relays.remove(relay->m_interval);
            delete relay;
        } else {
            disconnect(relay, SIGNAL(dataUpdated(QString,Plasma::DataEngine::Data)),
                       visualization, SLOT(dataUpdated(QString,Plasma::DataEngine::Data)));
        }
    }

    d->relayObjects.erase(objIt);
    checkUsage();
}

} // Plasma namespace

#include "datacontainer.moc"
#include "datacontainer_p.moc"

