// -*-c++-*-

/*!
  \file lofted_pass_generator.cpp
  \brief lofted (airborne) pass course generator Source File (v20 3D ball extension)
*/

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

#include "lofted_pass_generator.h"

#include "pass.h"

#include <rcsc/player/world_model.h>
#include <rcsc/player/abstract_player_object.h>
#include <rcsc/common/server_param.h>
#include <rcsc/common/player_type.h>
#include <rcsc/common/logger.h>
#include <rcsc/geom/vector_2d.h>
#include <rcsc/types.h>

#include <cmath>
#include <limits>

using namespace rcsc;

namespace {
const double DIR_STEP = 360.0 / LoftedPassGenerator::ANGLE_DIVS; // 30 deg
const double POWERS[ LoftedPassGenerator::NUM_POWERS ] = { 50.0, 80.0, 100.0 };
const double LOFTS[ LoftedPassGenerator::NUM_LOFTS ] = { 30.0, 45.0, 60.0 };
}

/*-------------------------------------------------------------------*/
/*!

 */
LoftedPassGenerator::LoftedPassGenerator()
    : M_update_time( -1, 0 )
{
    M_courses.reserve( LoftedPassGenerator::ANGLE_DIVS
                       * LoftedPassGenerator::NUM_POWERS
                       * LoftedPassGenerator::NUM_LOFTS );
    clear();
}

/*-------------------------------------------------------------------*/
/*!

 */
LoftedPassGenerator &
LoftedPassGenerator::instance()
{
    static LoftedPassGenerator s_instance;
    return s_instance;
}

/*-------------------------------------------------------------------*/
/*!

 */
void
LoftedPassGenerator::clear()
{
    M_courses.clear();
}

/*-------------------------------------------------------------------*/
/*!

 */
void
LoftedPassGenerator::generate( const WorldModel & wm )
{
    if ( M_update_time == wm.time() )
    {
        return;
    }
    M_update_time = wm.time();

    clear();

    if ( ! wm.self().isKickable() )
    {
        return;
    }

    createCourses( wm );
}

/*-------------------------------------------------------------------*/
/*!

 */
void
LoftedPassGenerator::createCourses( const WorldModel & wm )
{
    const ServerParam & SP = ServerParam::i();

    for ( int a = 0; a < ANGLE_DIVS; ++a )
    {
        const double dir_deg = a * DIR_STEP;

        for ( int p = 0; p < NUM_POWERS; ++p )
        {
            const double power = std::min( POWERS[p], SP.maxPower() );

            for ( int l = 0; l < NUM_LOFTS; ++l )
            {
                const double loft_deg = LOFTS[l];

                CooperativeAction::Ptr candidate
                    = simulateCandidate( wm, dir_deg, power, loft_deg );

                if ( candidate )
                {
                    M_courses.push_back( candidate );
                }
            }
        }
    }
}

/*-------------------------------------------------------------------*/
/*!

 */
CooperativeAction::Ptr
LoftedPassGenerator::simulateCandidate( const WorldModel & wm,
                                        const double & dir_deg,
                                        const double & power,
                                        const double & loft_deg ) const
{
    const ServerParam & SP = ServerParam::i();

    const Vector2D ball_pos = wm.ball().pos();
    const Vector2D ball_vel = ( wm.ball().velValid()
                               ? wm.ball().vel()
                               : Vector2D( 0.0, 0.0 ) );

    //
    // forward kick model: same (power, kickRate) -> ground-plane speed
    // relationship used by Body_SimpleKick3D/Body_KickOneStep, run
    // forward instead of solved for. loft split mirrors rcssserver's
    // Player::kickImpl() (loftPowerCost reduces total available push,
    // then cos/sin splits it into horizontal/vertical components).
    //
    const double loft_rad = loft_deg * M_PI / 180.0;
    double eff_power_total = power * wm.self().kickRate();
    eff_power_total *= ( 1.0 - SP.loftPowerCost() * ( loft_rad / ( M_PI * 0.5 ) ) );
    if ( eff_power_total < 0.0 )
    {
        eff_power_total = 0.0;
    }

    const double horiz_speed = eff_power_total * std::cos( loft_rad );
    const double vz0 = eff_power_total * std::sin( loft_rad );

    const AngleDeg dir( dir_deg );
    const Vector2D kick_accel = Vector2D::polar2vector( horiz_speed, dir );

    Vector2D vel_xy = ball_vel + kick_accel;
    if ( vel_xy.r() > SP.ballSpeedMax() )
    {
        vel_xy.setLength( SP.ballSpeedMax() );
    }
    const double first_speed = vel_xy.r();

    const double z0 = wm.ball().posZ();
    const double g = SP.gravity();
    const double player_height = SP.playerHeight();

    const double decay = SP.ballDecay();

    // running sum of the geometric decay series (1 + decay + decay^2 + ...),
    // accumulated incrementally across the t loop below instead of being
    // recomputed from scratch each cycle (O(t) total instead of O(t^2)).
    double decay_sum = 0.0;
    double decay_pow = 1.0;

    for ( int t = 1; t <= MAX_SIMULATION_STEP; ++t )
    {
        // ground-plane position: same decaying-velocity inertia model as
        // rcsc::BallObject::inertiaPoint()/PlayerType::inertiaPoint().
        decay_sum += decay_pow;
        decay_pow *= decay;

        const Vector2D pos_t = ball_pos + vel_xy * decay_sum;

        // out of pitch: this candidate is not viable, stop simulating it.
        if ( std::fabs( pos_t.x ) > SP.pitchHalfLength() + 5.0
             || std::fabs( pos_t.y ) > SP.pitchHalfWidth() + 5.0 )
        {
            return CooperativeAction::Ptr();
        }

        // closed-form vertical position (rcsc::InterceptSimulatorSelf3D's
        // z(t) = z0 + t*vz0 - g*t*(t+1)/2 recurrence), clamped at ground.
        double z_t = z0 + t * vz0 - 0.5 * g * t * ( t + 1 );
        if ( z_t < 0.0 )
        {
            z_t = 0.0;
        }

        if ( z_t > player_height )
        {
            // still too high for anyone to touch it yet.
            continue;
        }

        const AbstractPlayerObject * receiver = nearestPlayer( wm, pos_t );

        if ( ! receiver )
        {
            continue;
        }

        const double dist = receiver->pos().dist( pos_t );
        const int reach_step = ( receiver->playerTypePtr()
                                 ? receiver->playerTypePtr()->cyclesToReachDistance( dist )
                                 : static_cast< int >( std::ceil( dist / 1.0 ) ) );

        if ( reach_step > t )
        {
            // nobody can actually get there in time yet, keep simulating.
            continue;
        }

        if ( receiver->side() != wm.ourSide() )
        {
            // an opponent gets there first: discard this candidate entirely.
            return CooperativeAction::Ptr();
        }

        if ( receiver->unum() == wm.self().unum() )
        {
            // passing to self is meaningless here.
            return CooperativeAction::Ptr();
        }

        // a teammate can receive the lofted ball at (pos_t, cycle t).
        CooperativeAction::Ptr pass( new Pass( wm.self().unum(),
                                               receiver->unum(),
                                               pos_t,
                                               first_speed,
                                               t,
                                               1,
                                               false,
                                               "loftedPass",
                                               loft_deg ) );
        return pass;
    }

    return CooperativeAction::Ptr();
}

/*-------------------------------------------------------------------*/
/*!

 */
const AbstractPlayerObject *
LoftedPassGenerator::nearestPlayer( const WorldModel & wm,
                                    const Vector2D & pos ) const
{
    const AbstractPlayerObject * best = static_cast< const AbstractPlayerObject * >( 0 );
    double best_dist2 = std::numeric_limits< double >::max();

    const AbstractPlayerObject::Cont & our_players = wm.ourPlayers();
    for ( AbstractPlayerObject::Cont::const_iterator it = our_players.begin();
          it != our_players.end();
          ++it )
    {
        if ( ! (*it) || (*it)->unum() == Unum_Unknown ) continue;
        const double d2 = (*it)->pos().dist2( pos );
        if ( d2 < best_dist2 )
        {
            best_dist2 = d2;
            best = *it;
        }
    }

    const AbstractPlayerObject::Cont & their_players = wm.theirPlayers();
    for ( AbstractPlayerObject::Cont::const_iterator it = their_players.begin();
          it != their_players.end();
          ++it )
    {
        if ( ! (*it) || (*it)->unum() == Unum_Unknown ) continue;
        const double d2 = (*it)->pos().dist2( pos );
        if ( d2 < best_dist2 )
        {
            best_dist2 = d2;
            best = *it;
        }
    }

    return best;
}
