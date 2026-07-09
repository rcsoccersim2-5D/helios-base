// -*-c++-*-

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3, or (at your option)
 any later version.

 This code is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this code; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "actgen_lofted_pass.h"

#include "lofted_pass_generator.h"

#include "action_state_pair.h"
#include "predict_state.h"

#include <rcsc/common/logger.h>

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

 */
void
ActGen_LoftedPass::generate( std::vector< ActionStatePair > * result,
                             const PredictState & state,
                             const WorldModel & wm,
                             const std::vector< ActionStatePair > & path ) const
{
    // generate only first actions -- same guard as ActGen_StrictCheckPass.
    if ( ! path.empty() )
    {
        return;
    }

    const std::vector< CooperativeAction::Ptr > &
        courses = LoftedPassGenerator::instance().courses( wm );

    const std::vector< CooperativeAction::Ptr >::const_iterator end = courses.end();
    for ( std::vector< CooperativeAction::Ptr >::const_iterator act = courses.begin();
          act != end;
          ++act )
    {
        if ( (*act)->targetPlayerUnum() == Unum_Unknown )
        {
            continue;
        }

        const AbstractPlayerObject * target_player = state.ourPlayer( (*act)->targetPlayerUnum() );

        if ( ! target_player )
        {
            continue;
        }

        result->push_back( ActionStatePair( *act,
                                            new PredictState( state,
                                                              (*act)->durationStep(),
                                                              (*act)->targetPlayerUnum(),
                                                              (*act)->targetPoint() ) ) );
    }
}
