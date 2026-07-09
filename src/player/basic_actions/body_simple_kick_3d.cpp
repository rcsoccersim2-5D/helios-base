// -*-c++-*-

/*!
  \file body_simple_kick_3d.cpp
  \brief one-step loft-kick action for an airborne ball (v20 3D ball extension).
*/

/*
 *Copyright:

 Copyright (C) Hidehisa AKIYAMA

 This code is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
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

#include "body_simple_kick_3d.h"

#include "kick_table.h"

#include <rcsc/player/player_agent.h>
#include <rcsc/common/logger.h>
#include <rcsc/common/server_param.h>

#include <algorithm>

using namespace rcsc;

/*-------------------------------------------------------------------*/
/*!

*/
bool
Body_SimpleKick3D::execute( PlayerAgent * agent )
{
    dlog.addText( Logger::KICK,
                  __FILE__": Body_SimpleKick3D" );

    const WorldModel & wm = agent->world();

    if ( wm.ball().isGrounded() )
    {
        // ball is on the ground: hand off to the existing, unmodified
        // Body_SmartKick / KickTable / Body_KickOneStep for this (common) case.
        dlog.addText( Logger::KICK,
                      __FILE__": ball is grounded. declining, use the 2D kick primitives instead" );
        return false;
    }

    if ( ! wm.self().isKickable() )
    {
        std::cerr << __FILE__ << ": " << __LINE__
                  << " not ball kickable!"
                  << std::endl;
        dlog.addText( Logger::ACTION,
                      __FILE__": not kickable" );
        return false;
    }

    Vector2D ball_vel = wm.ball().vel();

    if ( ! wm.ball().velValid() )
    {
        if ( ! M_force_mode )
        {
            dlog.addText( Logger::KICK,
                          __FILE__": unknown ball vel" );
            return false;
        }

        ball_vel.assign( 0.0, 0.0 );
    }

    M_first_speed = std::min( M_first_speed, ServerParam::i().ballSpeedMax() );

    const AngleDeg target_angle = ( M_target_point - wm.ball().pos() ).th();

    // reuse the same 2D kick-solver as Body_KickOneStep: the ground-plane
    // component of a lofted kick is still governed by the ordinary kick
    // physics/kickable-area model, only the wire command differs (loft arg).
    Vector2D first_vel = KickTable::calc_max_velocity( target_angle,
                                                        wm.self().kickRate(),
                                                        ball_vel );
    double first_speed = first_vel.r();

    if ( first_speed > M_first_speed )
    {
        first_vel.setLength( M_first_speed );
        first_speed = M_first_speed;
    }
    else
    {
        dlog.addText( Logger::KICK,
                      __FILE__": cannot get required vel. only angle adjusted" );
    }

    const Vector2D kick_accel = first_vel - ball_vel;

    double kick_power = kick_accel.r() / wm.self().kickRate();
    const AngleDeg kick_dir = kick_accel.th() - wm.self().body();

    if ( kick_power > ServerParam::i().maxPower() + 0.01 )
    {
        if ( ! M_force_mode )
        {
            dlog.addText( Logger::KICK,
                          __FILE__": could not reach the required speed. kick_power=%f",
                          kick_power );
            return false;
        }

        kick_power = ServerParam::i().maxPower();
    }

    dlog.addText( Logger::KICK,
                  __FILE__": first_speed=%.3f, angle=%.1f, power=%.1f, dir=%.1f, loft=%.1f",
                  first_vel.r(), first_vel.th().degree(),
                  kick_power, kick_dir.degree(), M_loft );

    M_ball_result_pos = wm.ball().pos() + first_vel;
    M_ball_result_vel = first_vel * ServerParam::i().ballDecay();

    return agent->doKick( kick_power, kick_dir, M_loft );
}
