/*
 * SegmentTracker.cpp
 *****************************************************************************
 * Copyright (C) 2014 - VideoLAN and VLC authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "SegmentTracker.hpp"
#include "playlist/AbstractPlaylist.hpp"
#include "playlist/BaseRepresentation.h"
#include "playlist/BaseAdaptationSet.h"
#include "playlist/Segment.h"
#include "logic/AbstractAdaptationLogic.h"

using namespace adaptative;
using namespace adaptative::logic;
using namespace adaptative::playlist;

SegmentTracker::SegmentTracker(AbstractAdaptationLogic *logic_, BaseAdaptationSet *adaptSet)
{
    count = 0;
    initializing = true;
    index_sent = false;
    init_sent = false;
    sequence_set = false;
    prevRepresentation = NULL;
    setAdaptationLogic(logic_);
    adaptationSet = adaptSet;
}

SegmentTracker::~SegmentTracker()
{

}

void SegmentTracker::setAdaptationLogic(AbstractAdaptationLogic *logic_)
{
    logic = logic_;
}

void SegmentTracker::resetCounter()
{
    sequence_set = false;
    prevRepresentation = NULL;
}

SegmentChunk * SegmentTracker::getNextChunk(bool switch_allowed)
{
    BaseRepresentation *rep;
    ISegment *segment;

    if(!adaptationSet)
        return NULL;

    /* Ensure we don't keep chaining init/index without data */
    if( initializing && prevRepresentation )
        switch_allowed = false;

    if( !switch_allowed ||
       (prevRepresentation && prevRepresentation->getSwitchPolicy() == SegmentInformation::SWITCH_UNAVAILABLE) )
        rep = prevRepresentation;
    else
        rep = logic->getCurrentRepresentation(adaptationSet);

    if ( rep == NULL )
            return NULL;

    if(rep != prevRepresentation)
    {
        prevRepresentation = rep;
        init_sent = false;
        index_sent = false;
        initializing = true;
    }

    /* Ensure ephemere content is updated/loaded */
    if(rep->needsUpdate())
        updateSelected();

    /* If we're starting, set the first segment number to download */
    if(!sequence_set)
    {
        if(! rep->getSegmentNumberByTime( VLC_TS_INVALID, &count ) )
        {
            msg_Warn( rep->getPlaylist()->getVLCObject(),
                      "Can't get first segment number for representation %s", rep->getID().str().c_str() );
            count = 0;
        }
        sequence_set = true;
    }

    if(!init_sent)
    {
        init_sent = true;
        segment = rep->getSegment(BaseRepresentation::INFOTYPE_INIT);
        if(segment)
            return segment->toChunk(count, rep);
    }

    if(!index_sent)
    {
        index_sent = true;
        segment = rep->getSegment(BaseRepresentation::INFOTYPE_INDEX);
        if(segment)
            return segment->toChunk(count, rep);
    }

    segment = rep->getSegment(BaseRepresentation::INFOTYPE_MEDIA, count);
    if(!segment)
    {
        resetCounter();
        return NULL;
    }
    /* stop initializing after 1st chunk */
    initializing = false;

    SegmentChunk *chunk = segment->toChunk(count, rep);
    if(chunk)
        count++;

    return chunk;
}

bool SegmentTracker::setPositionByTime(mtime_t time, bool restarted, bool tryonly)
{
    uint64_t segnumber;
    if(prevRepresentation &&
       prevRepresentation->getSegmentNumberByTime(time, &segnumber))
    {
        if(!tryonly)
            setPositionByNumber(segnumber, restarted);
        return true;
    }
    return false;
}

void SegmentTracker::setPositionByNumber(uint64_t segnumber, bool restarted)
{
    if(restarted)
    {
        initializing = true;
        index_sent = false;
        init_sent = false;
    }
    count = segnumber;
    sequence_set = true;
}

mtime_t SegmentTracker::getSegmentStart() const
{
    if(prevRepresentation && sequence_set)
        return prevRepresentation->getPlaybackTimeBySegmentNumber(count);
    else
        return 0;
}

void SegmentTracker::pruneFromCurrent()
{
    AbstractPlaylist *playlist = adaptationSet->getPlaylist();
    if(playlist->isLive() && sequence_set)
        playlist->pruneBySegmentNumber(count);
}

void SegmentTracker::updateSelected()
{
    if(prevRepresentation)
        prevRepresentation->runLocalUpdates(getSegmentStart(), count);
}
