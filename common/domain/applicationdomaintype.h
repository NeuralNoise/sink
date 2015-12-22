/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
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

#include <QSharedPointer>
#include <QVariant>
#include <QByteArray>
#include "bufferadaptor.h"

namespace Akonadi2 {

namespace ApplicationDomain {

/**
 * The domain type interface has two purposes:
 * * provide a unified interface to read buffers (for zero-copy reading)
 * * record changes to generate changesets for modifications
 *
 * ApplicationDomainTypes don't adhere to any standard and are meant to be extended frequently (hence the non-typesafe interface).
 */
class ApplicationDomainType {
public:
    typedef QSharedPointer<ApplicationDomainType> Ptr;

    ApplicationDomainType();
    ApplicationDomainType(const QByteArray &resourceInstanceIdentifier);
    ApplicationDomainType(const QByteArray &resourceInstanceIdentifier, const QByteArray &identifier, qint64 revision, const QSharedPointer<BufferAdaptor> &adaptor);
    ApplicationDomainType(const ApplicationDomainType &other);
    ApplicationDomainType& operator=(const ApplicationDomainType &other);

    template <typename DomainType>
    static typename DomainType::Ptr getInMemoryRepresentation(const ApplicationDomainType &domainType, const QList<QByteArray> properties = QList<QByteArray>())
    {
        auto memoryAdaptor = QSharedPointer<Akonadi2::ApplicationDomain::MemoryBufferAdaptor>::create(*(domainType.mAdaptor), properties);
        //The identifier still internal refers to the memory-mapped pointer, we need to copy the memory or it will become invalid
        return QSharedPointer<DomainType>::create(domainType.mResourceInstanceIdentifier, QByteArray(domainType.mIdentifier.constData(), domainType.mIdentifier.size()), domainType.mRevision, memoryAdaptor);
    }

    virtual ~ApplicationDomainType();

    virtual QVariant getProperty(const QByteArray &key) const;
    virtual void setProperty(const QByteArray &key, const QVariant &value);
    virtual QByteArrayList changedProperties() const;
    qint64 revision() const;
    QByteArray resourceInstanceIdentifier() const;
    QByteArray identifier() const;

private:
    QSharedPointer<BufferAdaptor> mAdaptor;
    QHash<QByteArray, QVariant> mChangeSet;
    /*
     * Each domain object needs to store the resource, identifier, revision triple so we can link back to the storage location.
     */
    QByteArray mResourceInstanceIdentifier;
    QByteArray mIdentifier;
    qint64 mRevision;
};

/*
 * Should this be specific to the synclistresultset, in other cases we may want to take revision and resource into account.
 */
inline bool operator==(const ApplicationDomainType& lhs, const ApplicationDomainType& rhs)
{
    return lhs.identifier() == rhs.identifier()
            && lhs.resourceInstanceIdentifier() == rhs.resourceInstanceIdentifier();
}

struct Event : public ApplicationDomainType {
    typedef QSharedPointer<Event> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Todo : public ApplicationDomainType {
    typedef QSharedPointer<Todo> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Calendar : public ApplicationDomainType {
    typedef QSharedPointer<Calendar> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Mail : public ApplicationDomainType {
    typedef QSharedPointer<Mail> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

struct Folder : public ApplicationDomainType {
    typedef QSharedPointer<Folder> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

/**
 * Represents an akonadi resource.
 * 
 * This type is used for configuration of resources,
 * and for creating and removing resource instances.
 */
struct AkonadiResource : public ApplicationDomainType {
    typedef QSharedPointer<AkonadiResource> Ptr;
    using ApplicationDomainType::ApplicationDomainType;
};

/**
 * All types need to be registered here an MUST return a different name.
 *
 * Do not store these types to disk, they may change over time.
 */
template<class DomainType>
QByteArray getTypeName();

template<>
QByteArray getTypeName<Event>();

template<>
QByteArray getTypeName<Todo>();

template<>
QByteArray getTypeName<AkonadiResource>();

template<>
QByteArray getTypeName<Mail>();

template<>
QByteArray getTypeName<Folder>();

/**
 * Type implementation.
 * 
 * Needs to be implemented for every application domain type.
 * Contains all non-resource specific, but type-specific code.
 */
template<typename DomainType>
class TypeImplementation;

}
}

Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::ApplicationDomainType)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::ApplicationDomainType::Ptr)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Event)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Event::Ptr)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Mail)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Mail::Ptr)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Folder)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::Folder::Ptr)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::AkonadiResource)
Q_DECLARE_METATYPE(Akonadi2::ApplicationDomain::AkonadiResource::Ptr)
