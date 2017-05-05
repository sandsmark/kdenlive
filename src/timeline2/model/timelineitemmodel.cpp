/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "timelineitemmodel.hpp"

#include "bin/model/markerlistmodel.hpp"
#include "clipmodel.hpp"
#include "compositionmodel.hpp"
#include "doc/docundostack.hpp"
#include "groupsmodel.hpp"
#include "macros.hpp"
#include "trackmodel.hpp"
#include <QDebug>
#include <QFileInfo>
#include <mlt++/MltProfile.h>
#include <mlt++/MltTractor.h>
#include <mlt++/MltTransition.h>
#include <utility>

TimelineItemModel::TimelineItemModel(Mlt::Profile *profile, std::weak_ptr<DocUndoStack> undo_stack, Mlt::Tractor mlt_timeline)
    : TimelineModel(profile, undo_stack, mlt_timeline)
{
}

std::shared_ptr<TimelineItemModel> TimelineItemModel::construct(Mlt::Profile *profile, std::shared_ptr<MarkerListModel> guideModel,
                                                                std::weak_ptr<DocUndoStack> undo_stack, Mlt::Tractor mlt_timeline)
{
    std::shared_ptr<TimelineItemModel> ptr(new TimelineItemModel(profile, std::move(undo_stack), mlt_timeline));
    ptr->m_groups = std::unique_ptr<GroupsModel>(new GroupsModel(ptr));
    guideModel->registerSnapModel(ptr->m_snaps);
    return ptr;
}

TimelineItemModel::~TimelineItemModel()
{
}

QModelIndex TimelineItemModel::index(int row, int column, const QModelIndex &parent) const
{
    READ_LOCK();
    QModelIndex result;
    if (parent.isValid()) {
        int trackId = int(parent.internalId());
        Q_ASSERT(isTrack(trackId));
        int clipId = getTrackById_const(trackId)->getClipByRow(row);
        if (clipId != -1) {
            result = createIndex(row, 0, quintptr(clipId));
        } else {
            int compoId = getTrackById_const(trackId)->getCompositionByRow(row);
            if (compoId != -1) {
                result = createIndex(row, 0, quintptr(compoId));
            }
        }
    } else if (row < getTracksCount() && row >= 0) {
        auto it = m_allTracks.cbegin();
        std::advance(it, row);
        int trackId = (*it)->getId();
        result = createIndex(row, column, quintptr(trackId));
    }
    return result;
}

/*QModelIndex TimelineItemModel::makeIndex(int trackIndex, int clipIndex) const
{
    return index(clipIndex, 0, index(trackIndex));
}*/

QModelIndex TimelineItemModel::makeClipIndexFromID(int clipId) const
{
    Q_ASSERT(m_allClips.count(clipId) > 0);
    int trackId = m_allClips.at(clipId)->getCurrentTrackId();
    return index(getTrackById_const(trackId)->getRowfromClip(clipId), 0, makeTrackIndexFromID(trackId));
}

QModelIndex TimelineItemModel::makeCompositionIndexFromID(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    int trackId = m_allCompositions.at(compoId)->getCurrentTrackId();
    return index(getTrackById_const(trackId)->getRowfromComposition(compoId), 0, makeTrackIndexFromID(trackId));
}

QModelIndex TimelineItemModel::makeTrackIndexFromID(int trackId) const
{
    // we retrieve iterator
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    auto it = m_iteratorTable.at(trackId);
    int ind = (int)std::distance<decltype(m_allTracks.cbegin())>(m_allTracks.begin(), it);
    return index(ind);
}

QModelIndex TimelineItemModel::parent(const QModelIndex &index) const
{
    READ_LOCK();
    // qDebug() << "TimelineItemModel::parent"<< index;
    if (index == QModelIndex()) {
        return index;
    }
    const int id = static_cast<int>(index.internalId());
    if (!index.isValid() || isTrack(id)) {
        return QModelIndex();
    }
    if (isClip(id)) {
        const int trackId = getClipTrackId(id);
        return makeTrackIndexFromID(trackId);
    }
    if (isComposition(id)) {
        const int trackId = getCompositionTrackId(id);
        return makeTrackIndexFromID(trackId);
    }
    return QModelIndex();
}

int TimelineItemModel::rowCount(const QModelIndex &parent) const
{
    READ_LOCK();
    if (parent.isValid()) {
        const int id = (int)parent.internalId();
        if (isClip(id) || isComposition(id) || !isTrack(id)) {
            // clips don't have children
            // if it is not a track and not a clip, it is something invalid
            return 0;
        }
        return getTrackClipsCount(id) + getTrackCompositionsCount(id);
    }
    return getTracksCount();
}

int TimelineItemModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QHash<int, QByteArray> TimelineItemModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[ResourceRole] = "resource";
    roles[ServiceRole] = "mlt_service";
    roles[BinIdRole] = "binId";
    roles[IsBlankRole] = "blank";
    roles[StartRole] = "start";
    roles[DurationRole] = "duration";
    roles[MarkersRole] = "markers";
    roles[InPointRole] = "in";
    roles[OutPointRole] = "out";
    roles[FramerateRole] = "fps";
    roles[GroupedRole] = "grouped";
    roles[IsMuteRole] = "mute";
    roles[IsHiddenRole] = "hidden";
    roles[IsAudioRole] = "audio";
    roles[AudioLevelsRole] = "audioLevels";
    roles[IsCompositeRole] = "composite";
    roles[IsLockedRole] = "locked";
    roles[FadeInRole] = "fadeIn";
    roles[FadeOutRole] = "fadeOut";
    roles[IsCompositionRole] = "isComposition";
    roles[FileHashRole] = "hash";
    roles[SpeedRole] = "speed";
    roles[HeightRole] = "trackHeight";
    roles[ItemIdRole] = "item";
    roles[ItemATrack] = "a_track";
    roles[HasAudio] = "hasAudio";
    return roles;
}

QVariant TimelineItemModel::data(const QModelIndex &index, int role) const
{
    READ_LOCK();
    if (!m_tractor || !index.isValid()) {
        // qDebug() << "DATA abort. Index validity="<<index.isValid();
        return QVariant();
    }
    const int id = (int)index.internalId();
    if (role == ItemIdRole) {
        return id;
    }
    // qDebug() << "REQUESTING DATA "<<roleNames()[role]<<index;
    if (isClip(id)) {
        std::shared_ptr<ClipModel> clip = m_allClips.at(id);
        // Get data for a clip
        switch (role) {
        // TODO
        case NameRole:
        case Qt::DisplayRole: {
            QString result = clip->getProperty("kdenlive:clipname");
            if (result.isEmpty()) {
                result = clip->getProperty("resource");
                if (!result.isEmpty()) {
                    result = QFileInfo(result).fileName();
                } else {
                    result = clip->getProperty("mlt_service");
                }
            }
            return result;
        }
        case ResourceRole: {
            QString result = clip->getProperty("resource");
            if (result == QLatin1String("<producer>")) {
                result = clip->getProperty("mlt_service");
            }
            return result;
        }
        case BinIdRole:
            return clip->binId();
        case ServiceRole:
            return clip->getProperty("mlt_service");
            break;
        case AudioLevelsRole:
            return clip->getAudioWaveform();
        case HasAudio:
            return clip->hasAudio();
        case IsAudioRole:
            return clip->isAudioOnly();
        case MarkersRole: {
            return QVariant::fromValue<MarkerListModel *>(clip->getMarkerModel().get());
        }
        case StartRole:
            return clip->getPosition();
        case DurationRole:
            return clip->getPlaytime();
        case GroupedRole:
            return m_groups->isInGroup(id);
        case InPointRole:
            return clip->getIn();
        case OutPointRole:
            return clip->getOut();
        case IsCompositionRole:
            return false;
        default:
            break;
        }
    } else if (isTrack(id)) {
        // qDebug() << "DATA REQUESTED FOR TRACK "<< id;
        switch (role) {
        case NameRole:
        case Qt::DisplayRole: {
            QString tName = getTrackById_const(id)->getProperty("kdenlive:track_name").toString();
            return tName;
        }
        case DurationRole:
            // qDebug() << "DATA yielding duration" << m_tractor->get_playtime();
            return m_tractor->get_playtime();
        case IsMuteRole:
            // qDebug() << "DATA yielding mute" << 0;
            return getTrackById_const(id)->getProperty("hide").toInt() & 2;
        case IsHiddenRole:
            return getTrackById_const(id)->getProperty("hide").toInt() & 1;
        case IsAudioRole:
            return getTrackById_const(id)->getProperty("kdenlive:audio_track").toInt() == 1;
        case IsLockedRole:
            return getTrackById_const(id)->getProperty("kdenlive:locked_track").toInt() == 1;
        case HeightRole: {
            int height = getTrackById_const(id)->getProperty("kdenlive:trackheight").toInt();
            // qDebug() << "DATA yielding height" << height;
            return (height > 0 ? height : 60);
        }
        case IsCompositeRole: {
            return Qt::Unchecked;
        }
        default:
            break;
        }
    } else if (isComposition(id)) {
        std::shared_ptr<CompositionModel> compo = m_allCompositions.at(id);
        switch (role) {
        case NameRole:
        case Qt::DisplayRole:
        case ResourceRole:
        case ServiceRole:
            return compo->getProperty("mlt_service");
            break;
        case IsBlankRole: // probably useless
            return false;
        case StartRole:
            return compo->getPosition();
        case DurationRole:
            return compo->getPlaytime();
        case GroupedRole:
            return false; // m_groups->isInGroup(id);
        case InPointRole:
            return 0;
        case OutPointRole:
            return 100;
        case BinIdRole:
            return 5;
        case ItemATrack:
            return compo->getATrack();
        case MarkersRole: {
            QVariantList markersList;
            return markersList;
        }
        case IsCompositionRole:
            return true;
        default:
            break;
        }
    } else {
        qDebug() << "UNKNOWN DATA requested " << index << roleNames()[role];
    }
    return QVariant();
}

void TimelineItemModel::setTrackProperty(int trackId, const QString &name, const QString &value)
{
    getTrackById(trackId)->setProperty(name, value);
    QVector<int> roles;
    if (name == QLatin1String("kdenlive:track_name")) {
        roles.push_back(NameRole);
    } else if (name == QLatin1String("kdenlive:locked_track")) {
        roles.push_back(IsLockedRole);
    } else if (name == QLatin1String("hide")) {
        roles.push_back(IsMuteRole);
        roles.push_back(IsHiddenRole);
    }
    if (!roles.isEmpty()) {
        QModelIndex ix = makeTrackIndexFromID(trackId);
        emit dataChanged(ix, ix, roles);
    }
}

void TimelineItemModel::notifyChange(const QModelIndex &topleft, const QModelIndex &bottomright, bool start, bool duration, bool updateThumb)
{
    QVector<int> roles;
    if (start) {
        roles.push_back(StartRole);
        if (updateThumb) {
            roles.push_back(InPointRole);
        }
    }
    if (duration) {
        roles.push_back(DurationRole);
        if (updateThumb) {
            roles.push_back(OutPointRole);
        }
    }
    emit dataChanged(topleft, bottomright, roles);
}

void TimelineItemModel::notifyChange(const QModelIndex &topleft, const QModelIndex &bottomright, QVector<int> roles)
{
    emit dataChanged(topleft, bottomright, roles);
}

void TimelineItemModel::_beginRemoveRows(const QModelIndex &i, int j, int k)
{
    // qDebug()<<"FORWARDING beginRemoveRows"<<i<<j<<k;
    beginRemoveRows(i, j, k);
}
void TimelineItemModel::_beginInsertRows(const QModelIndex &i, int j, int k)
{
    // qDebug()<<"FORWARDING beginInsertRows"<<i<<j<<k;
    beginInsertRows(i, j, k);
}
void TimelineItemModel::_endRemoveRows()
{
    // qDebug()<<"FORWARDING endRemoveRows";
    endRemoveRows();
}
void TimelineItemModel::_endInsertRows()
{
    // qDebug()<<"FORWARDING endinsertRows";
    endInsertRows();
}

void TimelineItemModel::_resetView()
{
    beginResetModel();
    endResetModel();
}
