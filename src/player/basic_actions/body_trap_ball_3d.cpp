// -*-c++-*-

/*!
  \file body_trap_ball_3d.cpp
  \brief trap (deaden) an airborne ball using the v20 (chest_trap) command.
*/

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 3 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 *EndCopyright:
 */

/////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "body_trap_ball_3d.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/common/logger.h>

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

*/
bool
Body_TrapBall3D::execute( PlayerAgent * agent )
{
    dlog.addText( Logger::ACTION,
                  __FILE__": Body_TrapBall3D" );

    const WorldModel & wm = agent->world();

    if ( ! wm.ball().posZValid()
         || wm.ball().posZ() <= 0.0 )
    {
        // Ball height is unavailable/stale or grounded: this class only
        // targets a fresh airborne observation.
        // caller should use the existing Body_StopBall (or an ordinary kick)
        // instead.
        dlog.addText( Logger::ACTION,
                      __FILE__": ball is grounded. declining" );
        return false;
    }

    if ( ! wm.self().isKickable() )
    {
        // mirrors the server's own silent-reject gate on (chest_trap) --
        // no point issuing the command when it will be rejected.
        dlog.addText( Logger::ACTION,
                      __FILE__": ball is airborne but not kickable. declining" );
        return false;
    }

    return agent->doChestTrap();
}
