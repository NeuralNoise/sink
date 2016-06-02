/*
 * Copyright (C) 2016 Christian Mollekopf <mollekopf@kolabsys.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "sink_export.h"
#include <domainadaptor.h>

#include "storage.h"
#include "adaptorfactoryregistry.h"

namespace Sink {

class SINK_EXPORT EntityStore
{
public:
    EntityStore(const QByteArray &resourceType, const QByteArray &mResourceInstanceIdentifier, Sink::Storage::Transaction &transaction);

    template<typename T>
    T read(const QByteArray &identifier) const
    {
        auto typeName = ApplicationDomain::getTypeName<T>();
        auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
        auto bufferAdaptor = getLatest(mainDatabase, identifier, *Sink::AdaptorFactoryRegistry::instance().getFactory<T>(mResourceType));
        if (!bufferAdaptor) {
            return T();
        }
        return T(mResourceInstanceIdentifier, identifier, 0, bufferAdaptor);
    }

    template<typename T>
    T readFromKey(const QByteArray &key) const
    {
        auto typeName = ApplicationDomain::getTypeName<T>();
        auto mainDatabase = Storage::mainDatabase(mTransaction, typeName);
        auto bufferAdaptor = get(mainDatabase, key, *Sink::AdaptorFactoryRegistry::instance().getFactory<T>(mResourceType));
        const auto identifier = Storage::uidFromKey(key);
        if (!bufferAdaptor) {
            return T();
        }
        return T(mResourceInstanceIdentifier, identifier, 0, bufferAdaptor);
    }


    static QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> getLatest(const Sink::Storage::NamedDatabase &db, const QByteArray &uid, DomainTypeAdaptorFactoryInterface &adaptorFactory);
    static QSharedPointer<Sink::ApplicationDomain::BufferAdaptor> get(const Sink::Storage::NamedDatabase &db, const QByteArray &key, DomainTypeAdaptorFactoryInterface &adaptorFactory);
private:
    QByteArray mResourceType;
    QByteArray mResourceInstanceIdentifier;
    Sink::Storage::Transaction &mTransaction;
};

}
