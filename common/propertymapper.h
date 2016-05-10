/*
 * Copyright (C) 2014 Christian Mollekopf <chrigi_1@fastmail.fm>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#pragma once

#include "sink_export.h"
#include <QVariant>
#include <QByteArray>
#include <functional>
#include <flatbuffers/flatbuffers.h>

/**
 * Defines how to convert qt primitives to flatbuffer ones
 */
template <class T>
flatbuffers::uoffset_t SINK_EXPORT variantToProperty(const QVariant &, flatbuffers::FlatBufferBuilder &fbb);

/**
 * Defines how to convert flatbuffer primitives to qt ones
 */
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::String *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(uint8_t);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<uint8_t> *);
template <typename T>
QVariant SINK_EXPORT propertyToVariant(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *);

/**
 * The property mapper is a non-typesafe virtual dispatch.
 *
 * Instead of using an interface and requring each implementation to override
 * a virtual method per property, the property mapper can be filled with accessors
 * that extract the properties from resource types.
 */
template <typename BufferType>
class ReadPropertyMapper
{
public:
    virtual ~ReadPropertyMapper(){};

    virtual QVariant getProperty(const QByteArray &key, BufferType const *buffer) const
    {
        if (mReadAccessors.contains(key)) {
            auto accessor = mReadAccessors.value(key);
            return accessor(buffer);
        }
        return QVariant();
    }
    bool hasMapping(const QByteArray &key) const
    {
        return mReadAccessors.contains(key);
    }
    QList<QByteArray> availableProperties() const
    {
        return mReadAccessors.keys();
    }
    void addMapping(const QByteArray &property, const std::function<QVariant(BufferType const *)> &mapping)
    {
        mReadAccessors.insert(property, mapping);
    }

    template <typename T, typename Buffer>
    void addMapping(const flatbuffers::String *(Buffer::*f)() const)
    {
        addMapping(T::name, [f](Buffer const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((buffer->*f)()); });
    }

    template <typename T, typename Buffer>
    void addMapping(uint8_t (Buffer::*f)() const)
    {
        addMapping(T::name, [f](Buffer const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((buffer->*f)()); });
    }

    template <typename T, typename Buffer>
    void addMapping(bool (Buffer::*f)() const)
    {
        addMapping(T::name, [f](Buffer const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((buffer->*f)()); });
    }

    template <typename T, typename Buffer>
    void addMapping(const flatbuffers::Vector<uint8_t> *(Buffer::*f)() const)
    {
        addMapping(T::name, [f](Buffer const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((buffer->*f)()); });
    }

    template <typename T, typename Buffer>
    void addMapping(const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>> *(Buffer::*f)() const)
    {
        addMapping(T::name, [f](Buffer const *buffer) -> QVariant { return propertyToVariant<typename T::Type>((buffer->*f)()); });
    }

private:
    QHash<QByteArray, std::function<QVariant(BufferType const *)>> mReadAccessors;
};

template <typename BufferBuilder>
class WritePropertyMapper
{
public:
    virtual ~WritePropertyMapper(){};

    virtual void setProperty(const QByteArray &key, const QVariant &value, QList<std::function<void(BufferBuilder &)>> &builderCalls, flatbuffers::FlatBufferBuilder &fbb) const
    {
        if (mWriteAccessors.contains(key)) {
            auto accessor = mWriteAccessors.value(key);
            builderCalls << accessor(value, fbb);
        }
    }
    bool hasMapping(const QByteArray &key) const
    {
        return mWriteAccessors.contains(key);
    }
    void addMapping(const QByteArray &property, const std::function<std::function<void(BufferBuilder &)>(const QVariant &, flatbuffers::FlatBufferBuilder &)> &mapping)
    {
        mWriteAccessors.insert(property, mapping);
    }

    template <typename T>
    void addMapping(void (BufferBuilder::*f)(uint8_t))
    {
        addMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
            return [value, f](BufferBuilder &builder) { (builder.*f)(value.value<typename T::Type>()); };
        });
    }

    template <typename T>
    void addMapping(void (BufferBuilder::*f)(bool))
    {
        addMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
            return [value, f](BufferBuilder &builder) { (builder.*f)(value.value<typename T::Type>()); };
        });
    }

    template <typename T>
    void addMapping(void (BufferBuilder::*f)(flatbuffers::Offset<flatbuffers::String>))
    {
        addMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
            auto offset = variantToProperty<typename T::Type>(value, fbb);
            return [offset, f](BufferBuilder &builder) { (builder.*f)(offset); };
        });
    }

    template <typename T>
    void addMapping(void (BufferBuilder::*f)(flatbuffers::Offset<flatbuffers::Vector<uint8_t>>))
    {
        addMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
            auto offset = variantToProperty<typename T::Type>(value, fbb);
            return [offset, f](BufferBuilder &builder) { (builder.*f)(offset); };
        });
    }

    template <typename T>
    void addMapping(void (BufferBuilder::*f)(flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>>))
    {
        addMapping(T::name, [f](const QVariant &value, flatbuffers::FlatBufferBuilder &fbb) -> std::function<void(BufferBuilder &)> {
            auto offset = variantToProperty<typename T::Type>(value, fbb);
            return [offset, f](BufferBuilder &builder) { (builder.*f)(offset); };
        });
    }

private:
    QHash<QByteArray, std::function<std::function<void(BufferBuilder &)>(const QVariant &, flatbuffers::FlatBufferBuilder &)>> mWriteAccessors;
};
