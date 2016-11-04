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

#include "sink_export.h"
#include <QByteArrayList>
#include <QHash>
#include <QSet>
#include "applicationdomaintype.h"

namespace Sink {

class SINK_EXPORT QueryBase
{
public:
    struct Comparator {
        enum Comparators {
            Invalid,
            Equals,
            Contains,
            In
        };

        Comparator();
        Comparator(const QVariant &v);
        Comparator(const QVariant &v, Comparators c);
        bool matches(const QVariant &v) const;

        QVariant value;
        Comparators comparator;
    };

    class Filter {
    public:
        QByteArrayList ids;
        QHash<QByteArray, Comparator> propertyFilter;
    };

    Comparator getFilter(const QByteArray &property) const
    {
        return mBaseFilterStage.propertyFilter.value(property);
    }

    bool hasFilter(const QByteArray &property) const
    {
        return mBaseFilterStage.propertyFilter.contains(property);
    }

    void setBaseFilters(const QHash<QByteArray, Comparator> &filter)
    {
        mBaseFilterStage.propertyFilter = filter;
    }

    QHash<QByteArray, Comparator> getBaseFilters() const
    {
        return mBaseFilterStage.propertyFilter;
    }

    QByteArrayList ids() const
    {
        return mBaseFilterStage.ids;
    }

    void filter(const QByteArray &id)
    {
        mBaseFilterStage.ids << id;
    }

    void filter(const QByteArrayList &ids)
    {
        mBaseFilterStage.ids << ids;
    }

    void filter(const QByteArray &property, const QueryBase::Comparator &comparator)
    {
        mBaseFilterStage.propertyFilter.insert(property, comparator);
    }

    void setType(const QByteArray &type)
    {
        mType = type;
    }

    QByteArray type() const
    {
        return mType;
    }

private:
    Filter mBaseFilterStage;
    QByteArray mType;
};

/**
 * A query that matches a set of entities.
 */
class SINK_EXPORT Query : public QueryBase
{
public:
    enum Flag
    {
        /** Leave the query running and continuously update the result set. */
        LiveQuery,
        /** Run the query synchronously. */
        SynchronousQuery
    };
    Q_DECLARE_FLAGS(Flags, Flag)

    template <typename T>
    Query &request()
    {
        requestedProperties << T::name;
        return *this;
    }

    template <typename T>
    Query &requestTree()
    {
        parentProperty = T::name;
        return *this;
    }

    template <typename T>
    Query &sort()
    {
        sortProperty = T::name;
        return *this;
    }

    template <typename T>
    Query &filter(const QVariant &value)
    {
        filter(T::name, value);
        return *this;
    }

    template <typename T>
    Query &containsFilter(const QVariant &value)
    {
        QueryBase::filter(T::name, QueryBase::Comparator(value, QueryBase::Comparator::Contains));
        return *this;
    }

    template <typename T>
    Query &filter(const QueryBase::Comparator &comparator)
    {
        QueryBase::filter(T::name, comparator);
        return *this;
    }

    Query &filter(const QByteArray &id)
    {
        QueryBase::filter(id);
        return *this;
    }

    Query &filter(const QByteArrayList &ids)
    {
        QueryBase::filter(ids);
        return *this;
    }

    Query &filter(const QByteArray &property, const QueryBase::Comparator &comparator)
    {
        QueryBase::filter(property, comparator);
        return *this;
    }

    template <typename T>
    Query &filter(const ApplicationDomain::Entity &value)
    {
        filter(T::name, QVariant::fromValue(value.identifier()));
        return *this;
    }

    template <typename T>
    Query &filter(const Query &query)
    {
        auto q = query;
        q.setType(ApplicationDomain::getTypeName<typename T::ReferenceType>());
        filter(T::name, QVariant::fromValue(q));
        return *this;
    }


    Query(const ApplicationDomain::Entity &value) : limit(0), liveQuery(false), synchronousQuery(false)
    {
        filter(value.identifier());
        resourceFilter(value.resourceInstanceIdentifier());
    }

    Query(Flags flags = Flags()) : limit(0), liveQuery(false), synchronousQuery(false)
    {
    }

    QByteArrayList requestedProperties;
    QByteArray parentProperty;
    QByteArray sortProperty;
    int limit;
    bool liveQuery;
    bool synchronousQuery;

    class FilterStage {
    public:
        virtual ~FilterStage(){};
    };

    QList<QSharedPointer<FilterStage>> getFilterStages()
    {
        return mFilterStages;
    }

    Filter getResourceFilter() const
    {
        return mResourceFilter;
    }

    Query &resourceFilter(const QByteArray &id)
    {
        mResourceFilter.ids << id;
        return *this;
    }

    template <typename T>
    Query &resourceFilter(const ApplicationDomain::ApplicationDomainType &entity)
    {
        mResourceFilter.propertyFilter.insert(T::name, Comparator(entity.identifier()));
        return *this;
    }

    Query &resourceFilter(const QByteArray &name, const Comparator &comparator)
    {
        mResourceFilter.propertyFilter.insert(name, comparator);
        return *this;
    }

    template <typename T>
    Query &resourceContainsFilter(const QVariant &value)
    {
        return resourceFilter(T::name, Comparator(value, Comparator::Contains));
    }

    template <typename T>
    Query &resourceFilter(const QVariant &value)
    {
        return resourceFilter(T::name, value);
    }

    class Reduce : public FilterStage {
    public:

        class Selector {
        public:
            enum Comparator {
                Min, //get the minimum value
                Max, //get the maximum value
                First //Get the first result we get
            };

            template <typename SelectionProperty>
            static Selector max()
            {
                return Selector(SelectionProperty::name, Max);
            }

            Selector(const QByteArray &p, Comparator c)
                : property(p),
                comparator(c)
            {
            }

            QByteArray property;
            Comparator comparator;
        };

        class Aggregator {
        public:
            enum Operation {
                Count,
                Collect
            };

            Aggregator(const QByteArray &p, Operation o, const QByteArray &c = QByteArray())
                : resultProperty(p),
                operation(o),
                propertyToCollect(c)
            {
            }

            QByteArray resultProperty;
            Operation operation;
            QByteArray propertyToCollect;
        };

        Reduce(const QByteArray &p, const Selector &s)
            : property(p),
            selector(s)
        {
        }

        Reduce &count(const QByteArray &propertyName = "count")
        {
            aggregators << Aggregator(propertyName, Aggregator::Count);
            return *this;
        }

        template <typename T>
        Reduce &collect(const QByteArray &propertyName)
        {
            aggregators << Aggregator(propertyName, Aggregator::Collect, T::name);
            return *this;
        }

        //Reduce on property
        QByteArray property;
        Selector selector;
        QList<Aggregator> aggregators;

        //TODO add aggregate functions like:
        //.count()
        //.collect<Mail::sender>();
        //...
        //
        //Potentially pass-in an identifier under which the result will be available in the result set.
    };

    template <typename T>
    Reduce &reduce(const Reduce::Selector &s)
    {
        auto reduction = QSharedPointer<Reduce>::create(T::name, s);
        mFilterStages << reduction;
        return *reduction;
    }

    /**
    * "Bloom" on a property.
    *
    * For every encountered value of a property,
    * a result set is generated containing all entries with the same value.
    *
    * Example:
    * For an input set of one mail; return all emails with the same threadId.
    */
    class Bloom : public FilterStage {
    public:
        //Property to bloom on
        QByteArray property;
        Bloom(const QByteArray &p)
            : property(p)
        {
        }
    };

    template <typename T>
    void bloom()
    {
        auto bloom = QSharedPointer<Bloom>::create(T::name);
        mFilterStages << bloom;
    }

private:
    Filter mResourceFilter;
    QList<QSharedPointer<FilterStage>> mFilterStages;
};

}

QDebug operator<<(QDebug dbg, const Sink::Query::Comparator &c);

Q_DECLARE_OPERATORS_FOR_FLAGS(Sink::Query::Flags)
Q_DECLARE_METATYPE(Sink::Query);
