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
#include "entitystore.h"

#include "entitybuffer.h"
#include "log.h"
#include "typeindex.h"
#include "definitions.h"
#include "resourcecontext.h"
#include "index.h"
#include "bufferutils.h"
#include "entity_generated.h"

#include "mail.h"
#include "folder.h"
#include "event.h"

using namespace Sink;
using namespace Sink::Storage;

SINK_DEBUG_AREA("entitystore");

class EntityStore::Private {
public:
    Private(const ResourceContext &context) : resourceContext(context) {}

    ResourceContext resourceContext;
    DataStore::Transaction transaction;
    QHash<QByteArray, QSharedPointer<TypeIndex> > indexByType;

    DataStore::Transaction &getTransaction()
    {
        if (transaction) {
            return transaction;
        }

        Sink::Storage::DataStore store(Sink::storageLocation(), resourceContext.instanceId(), DataStore::ReadOnly);
        transaction = store.createTransaction(DataStore::ReadOnly);
        Q_ASSERT(transaction.validateNamedDatabases());
        return transaction;
    }

    /* template<typename T> */
    /* TypeIndex &typeIndex(const QByteArray &type) */
    /* { */
    /*     if (indexByType.contains(type)) { */
    /*         return *indexByType.value(type); */
    /*     } */
    /*     auto index = QSharedPointer<TypeIndex>::create(type); */
    /*     ApplicationDomain::TypeImplementation<T>::configureIndex(*index); */
    /*     indexByType.insert(type, index); */
    /*     return *index; */
    /* } */

    TypeIndex &typeIndex(const QByteArray &type)
    {
        /* return applyType<typeIndex>(type); */
        if (indexByType.contains(type)) {
            return *indexByType.value(type);
        }
        auto index = QSharedPointer<TypeIndex>::create(type);
        //TODO expand for all types
        /* TypeHelper<type>::configureIndex(*index); */
        // Try this: (T would i.e. become
        // TypeHelper<ApplicationDomain::TypeImplementation>::T::configureIndex(*index);
        if (type == ApplicationDomain::getTypeName<ApplicationDomain::Folder>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Folder>::configureIndex(*index);
        } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Mail>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Mail>::configureIndex(*index);
        } else if (type == ApplicationDomain::getTypeName<ApplicationDomain::Event>()) {
            ApplicationDomain::TypeImplementation<ApplicationDomain::Event>::configureIndex(*index);
        } else {
            Q_ASSERT(false);
            SinkError() << "Unkonwn type " << type;
        }
        indexByType.insert(type, index);
        return *index;
    }
};

EntityStore::EntityStore(const ResourceContext &context)
    : d(new EntityStore::Private{context})
{

}

void EntityStore::startTransaction(Sink::Storage::DataStore::AccessMode accessMode)
{
    Sink::Storage::DataStore store(Sink::storageLocation(), d->resourceContext.instanceId(), accessMode);
    d->transaction = store.createTransaction(accessMode);
    Q_ASSERT(d->transaction.validateNamedDatabases());
}

void EntityStore::commitTransaction()
{
    SinkTrace() << "Committing transaction";
    d->transaction.commit();
    d->transaction = Storage::DataStore::Transaction();
}

void EntityStore::abortTransaction()
{
    SinkTrace() << "Aborting transaction";
    d->transaction.abort();
    d->transaction = Storage::DataStore::Transaction();
}

bool EntityStore::add(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &entity_, bool replayToSource, const PreprocessCreation &preprocess)
{
    if (entity_.identifier().isEmpty()) {
        SinkWarning() << "Can't write entity with an empty identifier";
        return false;
    }

    auto entity = *ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<ApplicationDomain::ApplicationDomainType>(entity_, entity_.availableProperties());
    entity.setChangedProperties(entity.availableProperties().toSet());

    preprocess(entity);
    d->typeIndex(type).add(entity.identifier(), entity, d->transaction);

    //The maxRevision may have changed meanwhile if the entity created sub-entities
    const qint64 newRevision = maxRevision() + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Creation);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    d->resourceContext.adaptorFactory(type).createBuffer(entity, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(entity.identifier(), newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarning() << "Failed to write entity" << entity.identifier() << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, entity.identifier(), type);
    SinkTrace() << "Wrote entity: " << entity.identifier() << type << newRevision;
    return true;
}

bool EntityStore::modify(const QByteArray &type, const ApplicationDomain::ApplicationDomainType &diff, const QByteArrayList &deletions, bool replayToSource, const PreprocessModification &preprocess)
{
    auto changeset = diff.changedProperties();
    //TODO handle errors
    const auto current = readLatest(type, diff.identifier());
    if (current.identifier().isEmpty()) {
        SinkWarning() << "Failed to read current version: " << diff.identifier();
        return false;
    }

    auto newEntity = *ApplicationDomain::ApplicationDomainType::getInMemoryRepresentation<ApplicationDomain::ApplicationDomainType>(current, current.availableProperties());

    // Apply diff
    //SinkTrace() << "Applying changed properties: " << changeset;
    for (const auto &property : changeset) {
        const auto value = diff.getProperty(property);
        if (value.isValid()) {
            //SinkTrace() << "Setting property: " << property;
            newEntity.setProperty(property, value);
        }
    }

    // Remove deletions
    for (const auto property : deletions) {
        //SinkTrace() << "Removing property: " << property;
        newEntity.setProperty(property, QVariant());
    }

    preprocess(current, newEntity);
    d->typeIndex(type).remove(current.identifier(), current, d->transaction);
    d->typeIndex(type).add(newEntity.identifier(), newEntity, d->transaction);

    const qint64 newRevision = DataStore::maxRevision(d->transaction) + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    {
        //We add availableProperties to account for the properties that have been changed by the preprocessors
        auto modifiedProperties = BufferUtils::toVector(metadataFbb, changeset + newEntity.changedProperties());
        auto metadataBuilder = MetadataBuilder(metadataFbb);
        metadataBuilder.add_revision(newRevision);
        metadataBuilder.add_operation(Operation_Modification);
        metadataBuilder.add_replayToSource(replayToSource);
        metadataBuilder.add_modifiedProperties(modifiedProperties);
        auto metadataBuffer = metadataBuilder.Finish();
        FinishMetadataBuffer(metadataFbb, metadataBuffer);
    }

    newEntity.setChangedProperties(newEntity.availableProperties().toSet());
    SinkTrace() << "All properties: " << newEntity.availableProperties();

    flatbuffers::FlatBufferBuilder fbb;
    d->resourceContext.adaptorFactory(type).createBuffer(newEntity, fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize());

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(newEntity.identifier(), newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarning() << "Failed to write entity" << newEntity.identifier() << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, newEntity.identifier(), type);
    SinkTrace() << "Wrote modified entity: " << newEntity.identifier() << type << newRevision;
    return true;
}

bool EntityStore::remove(const QByteArray &type, const QByteArray &uid, bool replayToSource, const PreprocessRemoval &preprocess)
{
    bool found = false;
    bool alreadyRemoved = false;
    DataStore::mainDatabase(d->transaction, type)
        .findLatest(uid,
            [&found, &alreadyRemoved](const QByteArray &key, const QByteArray &data) -> bool {
                auto entity = GetEntity(data.data());
                if (entity && entity->metadata()) {
                    auto metadata = GetMetadata(entity->metadata()->Data());
                    found = true;
                    if (metadata->operation() == Operation_Removal) {
                        alreadyRemoved = true;
                    }
                }
                return false;
            },
            [](const DataStore::Error &error) { SinkWarning() << "Failed to read old revision from storage: " << error.message; });

    if (!found) {
        SinkWarning() << "Failed to find entity " << uid;
        return false;
    }
    if (alreadyRemoved) {
        SinkWarning() << "Entity is already removed " << uid;
        return false;
    }

    const auto current = readLatest(type, uid);
    preprocess(current);
    d->typeIndex(type).remove(current.identifier(), current, d->transaction);

    const qint64 newRevision = DataStore::maxRevision(d->transaction) + 1;

    // Add metadata buffer
    flatbuffers::FlatBufferBuilder metadataFbb;
    auto metadataBuilder = MetadataBuilder(metadataFbb);
    metadataBuilder.add_revision(newRevision);
    metadataBuilder.add_operation(Operation_Removal);
    metadataBuilder.add_replayToSource(replayToSource);
    auto metadataBuffer = metadataBuilder.Finish();
    FinishMetadataBuffer(metadataFbb, metadataBuffer);

    flatbuffers::FlatBufferBuilder fbb;
    EntityBuffer::assembleEntityBuffer(fbb, metadataFbb.GetBufferPointer(), metadataFbb.GetSize(), 0, 0, 0, 0);

    DataStore::mainDatabase(d->transaction, type)
        .write(DataStore::assembleKey(uid, newRevision), BufferUtils::extractBuffer(fbb),
            [&](const DataStore::Error &error) { SinkWarning() << "Failed to write entity" << uid << newRevision; });
    DataStore::setMaxRevision(d->transaction, newRevision);
    DataStore::recordRevision(d->transaction, newRevision, uid, type);
    return true;
}

void EntityStore::cleanupRevision(qint64 revision)
{
    const auto uid = DataStore::getUidFromRevision(d->transaction, revision);
    const auto bufferType = DataStore::getTypeFromRevision(d->transaction, revision);
    SinkTrace() << "Cleaning up revision " << revision << uid << bufferType;
    DataStore::mainDatabase(d->transaction, bufferType)
        .scan(uid,
            [&](const QByteArray &key, const QByteArray &data) -> bool {
                EntityBuffer buffer(const_cast<const char *>(data.data()), data.size());
                if (!buffer.isValid()) {
                    SinkWarning() << "Read invalid buffer from disk";
                } else {
                    const auto metadata = flatbuffers::GetRoot<Metadata>(buffer.metadataBuffer());
                    const qint64 rev = metadata->revision();
                    // Remove old revisions, and the current if the entity has already been removed
                    if (rev < revision || metadata->operation() == Operation_Removal) {
                        DataStore::removeRevision(d->transaction, rev);
                        DataStore::mainDatabase(d->transaction, bufferType).remove(key);
                    }
                }

                return true;
            },
            [](const DataStore::Error &error) { SinkWarning() << "Error while reading: " << error.message; }, true);
    DataStore::setCleanedUpRevision(d->transaction, revision);
}

void EntityStore::cleanupRevisions(qint64 revision)
{
    const auto lastCleanRevision = DataStore::cleanedUpRevision(d->transaction);
    SinkTrace() << "Cleaning up from " << lastCleanRevision + 1 << " to " << revision;
    for (qint64 rev = lastCleanRevision + 1; rev <= revision; rev++) {
        cleanupRevision(revision);
    }
}

QVector<QByteArray> EntityStore::fullScan(const QByteArray &type)
{
    SinkTrace() << "Looking for : " << type;
    //The scan can return duplicate results if we have multiple revisions, so we use a set to deduplicate.
    QSet<QByteArray> keys;
    DataStore::mainDatabase(d->getTransaction(), type)
        .scan(QByteArray(),
            [&](const QByteArray &key, const QByteArray &value) -> bool {
                const auto uid = DataStore::uidFromKey(key);
                if (keys.contains(uid)) {
                    //Not something that should persist if the replay works, so we keep a message for now.
                    SinkTrace() << "Multiple revisions for key: " << key;
                }
                keys << uid;
                return true;
            },
            [](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message; });

    SinkTrace() << "Full scan retrieved " << keys.size() << " results.";
    return keys.toList().toVector();
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const Query &query, QSet<QByteArray> &appliedFilters, QByteArray &appliedSorting)
{
    return d->typeIndex(type).query(query, appliedFilters, appliedSorting, d->getTransaction());
}

QVector<QByteArray> EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value)
{
    return d->typeIndex(type).lookup(property, value, d->getTransaction());
}

void EntityStore::indexLookup(const QByteArray &type, const QByteArray &property, const QVariant &value, const std::function<void(const QByteArray &uid)> &callback)
{
    auto list =  d->typeIndex(type).lookup(property, value, d->getTransaction());
    for (const auto &uid : list) {
        callback(uid);
    }
    /* Index index(type + ".index." + property, d->transaction); */
    /* index.lookup(value, [&](const QByteArray &sinkId) { */
    /*     callback(sinkId); */
    /* }, */
    /* [&](const Index::Error &error) { */
    /*     SinkWarning() << "Error in index: " <<  error.message << property; */
    /* }); */
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.findLatest(uid,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            callback(DataStore::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
            return false;
        },
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message << uid; });
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readLatest(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
    });
}

void EntityStore::readLatest(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &, Sink::Operation)> callback)
{
    readLatest(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        //TODO cache max revision for the duration of the transaction.
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor}, buffer.operation());
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readLatest(const QByteArray &type, const QByteArray &uid)
{
    ApplicationDomain::ApplicationDomainType dt;
    readLatest(type, uid, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}

void EntityStore::readEntity(const QByteArray &type, const QByteArray &key, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan(key,
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            callback(DataStore::uidFromKey(key), Sink::EntityBuffer(value.data(), value.size()));
            return false;
        },
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message << key; });
}

void EntityStore::readEntity(const QByteArray &type, const QByteArray &uid, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readEntity(type, uid, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readEntity(const QByteArray &type, const QByteArray &uid)
{
    ApplicationDomain::ApplicationDomainType dt;
    readEntity(type, uid, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}


void EntityStore::readAll(const QByteArray &type, const std::function<void(const ApplicationDomain::ApplicationDomainType &entity)> &callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan("",
        [=](const QByteArray &key, const QByteArray &value) -> bool {
            auto uid = DataStore::uidFromKey(key);
            auto buffer = Sink::EntityBuffer{value.data(), value.size()};
            auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
            callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
            return true;
        },
        [&](const DataStore::Error &error) { SinkWarning() << "Error during query: " << error.message; });
}

void EntityStore::readRevisions(qint64 baseRevision, const QByteArray &expectedType, const std::function<void(const QByteArray &key)> &callback)
{
    qint64 revisionCounter = baseRevision;
    const qint64 topRevision = DataStore::maxRevision(d->getTransaction());
    // Spit out the revision keys one by one.
    while (revisionCounter <= topRevision) {
        const auto uid = DataStore::getUidFromRevision(d->getTransaction(), revisionCounter);
        const auto type = DataStore::getTypeFromRevision(d->getTransaction(), revisionCounter);
        // SinkTrace() << "Revision" << *revisionCounter << type << uid;
        Q_ASSERT(!uid.isEmpty());
        Q_ASSERT(!type.isEmpty());
        if (type != expectedType) {
            // Skip revision
            revisionCounter++;
            continue;
        }
        const auto key = DataStore::assembleKey(uid, revisionCounter);
        revisionCounter++;
        callback(key);
    }
}

void EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const QByteArray &uid, const EntityBuffer &entity)> callback)
{
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    qint64 latestRevision = 0;
    db.scan(uid,
        [&latestRevision, revision](const QByteArray &key, const QByteArray &) -> bool {
            const auto foundRevision = Sink::Storage::DataStore::revisionFromKey(key);
            if (foundRevision < revision && foundRevision > latestRevision) {
                latestRevision = foundRevision;
            }
            return true;
        },
        [](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; }, true);
    return readEntity(type, Sink::Storage::DataStore::assembleKey(uid, latestRevision), callback);
}

void EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision, const std::function<void(const ApplicationDomain::ApplicationDomainType &)> callback)
{
    readPrevious(type, uid, revision, [&](const QByteArray &uid, const EntityBuffer &buffer) {
        auto adaptor = d->resourceContext.adaptorFactory(type).createAdaptor(buffer.entity());
        callback(ApplicationDomain::ApplicationDomainType{d->resourceContext.instanceId(), uid, DataStore::maxRevision(d->getTransaction()), adaptor});
    });
}

ApplicationDomain::ApplicationDomainType EntityStore::readPrevious(const QByteArray &type, const QByteArray &uid, qint64 revision)
{
    ApplicationDomain::ApplicationDomainType dt;
    readPrevious(type, uid, revision, [&](const ApplicationDomain::ApplicationDomainType &entity) {
        dt = entity;
    });
    return dt;
}

void EntityStore::readAllUids(const QByteArray &type, const std::function<void(const QByteArray &uid)> callback)
{
    //TODO use uid index instead
    //FIXME we currently report each uid for every revision with the same uid
    auto db = DataStore::mainDatabase(d->getTransaction(), type);
    db.scan("",
        [callback](const QByteArray &key, const QByteArray &) -> bool {
            callback(Sink::Storage::DataStore::uidFromKey(key));
            return true;
        },
        [](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Failed to read current value from storage: " << error.message; });
}

bool EntityStore::contains(const QByteArray &type, const QByteArray &uid)
{
    return DataStore::mainDatabase(d->getTransaction(), type).contains(uid);
}

qint64 EntityStore::maxRevision()
{
    return DataStore::maxRevision(d->getTransaction());
}

/* DataStore::Transaction getTransaction() */
/* { */
/*     Sink::Storage::DataStore::Transaction transaction; */
/*     { */
/*         Sink::Storage::DataStore storage(Sink::storageLocation(), mResourceInstanceIdentifier); */
/*         if (!storage.exists()) { */
/*             //This is not an error if the resource wasn't started before */
/*             SinkLog() << "Store doesn't exist: " << mResourceInstanceIdentifier; */
/*             return Sink::Storage::DataStore::Transaction(); */
/*         } */
/*         storage.setDefaultErrorHandler([this](const Sink::Storage::DataStore::Error &error) { SinkWarning() << "Error during query: " << error.store << error.message; }); */
/*         transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly); */
/*     } */

/*     //FIXME this is a temporary measure to recover from a failure to open the named databases correctly. */
/*     //Once the actual problem is fixed it will be enough to simply crash if we open the wrong database (which we check in openDatabase already). */
/*     while (!transaction.validateNamedDatabases()) { */
/*         Sink::Storage::DataStore storage(Sink::storageLocation(), mResourceInstanceIdentifier); */
/*         transaction = storage.createTransaction(Sink::Storage::DataStore::ReadOnly); */
/*     } */
/*     return transaction; */
/* } */