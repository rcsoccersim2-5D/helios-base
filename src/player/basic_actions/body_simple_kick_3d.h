// -*-c++-*-

/*!
  \file body_simple_kick_3d.h
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

#ifndef RCSC_ACTION_BODY_SIMPLE_KICK_3D_H
#define RCSC_ACTION_BODY_SIMPLE_KICK_3D_H

#include <rcsc/player/soccer_action.h>
#include <rcsc/geom/vector_2d.h>

/*!
  \class Body_SimpleKick3D
  \brief one-step loft-kick action, only for an AIRBORNE ball (v20 3D ball
  extension).

  This class's very first precondition is `wm.ball().isGrounded()` (librcsc
  v20 BallObject belief): when the ball IS grounded, this class declines to
  act (`execute()` returns false), so the caller should fall back to
  helios-base's existing, completely UNMODIFIED Body_KickOneStep / Body_SmartKick
  / KickTable forks (basic_actions/body_kick_one_step.h, body_smart_kick.h,
  kick_table.h) for that (still by far the most common) case.

  When the ball is airborne (the only case this class targets), it reuses
  helios-base's own forked KickTable::calc_max_velocity() to solve for the 2D
  kick power/direction exactly like Body_KickOneStep, then forwards the result
  through librcsc's v20 PlayerAgent::doKick(power, dir, loft) 3-arg overload so
  the server receives a lofted kick command instead of a flat one.

  This mirrors librcsc's own rcsc::Body_SmartKick3D (rcsc/action/body_smart_kick_3d.h)
  one-for-one, since helios-base independently forks the identical
  KickTable/Body_SmartKick/Body_KickOneStep classes and therefore needs its own
  gated wrapper -- librcsc's new class does NOT automatically make helios-base's
  kick primitives 3D-aware.

  NOTE: This is a true ONE-STEP primitive (same simplicity level as
  Body_KickOneStep) -- it does NOT reimplement KickTable's multi-step search,
  and it deliberately does NOT fall back to Body_StopBall()/Body_HoldBall2008()
  on failure (unlike Body_KickOneStep), because both of those existing helpers
  assume a grounded ball. Trapping an airborne ball is handled by the new
  Body_TrapBall3D class (basic_actions/body_trap_ball_3d.h), not by this class.
*/
class Body_SimpleKick3D
    : public rcsc::BodyAction {
private:
    //! target point where ball should reach or pass through (ground-plane projection)
    const rcsc::Vector2D M_target_point;
    //! ball first speed when ball is released
    double M_first_speed;
    //! loft angle forwarded to doKick() (0 = flat/grounded, higher = more airborne arc)
    const double M_loft;
    //! force mode flag: if true, clamp to reachable power instead of declining
    const bool M_force_mode;

    //! result ball position
    rcsc::Vector2D M_ball_result_pos;
    //! result ball velocity
    rcsc::Vector2D M_ball_result_vel;

public:
    /*!
      \brief construct with all parameters
      \param target_point global coordinate of the target position (ground-plane)
      \param first_speed desired ball first speed when ball is released
      \param loft desired loft angle, forwarded verbatim to doKick()
      \param force_mode enforce to kick out even if the exact speed cannot be reached
    */
    Body_SimpleKick3D( const rcsc::Vector2D & target_point,
                       const double & first_speed,
                       const double & loft,
                       const bool force_mode = false )
        : M_target_point( target_point ),
          M_first_speed( first_speed ),
          M_loft( loft ),
          M_force_mode( force_mode ),
          M_ball_result_pos( rcsc::Vector2D::INVALIDATED ),
          M_ball_result_vel( rcsc::Vector2D::INVALIDATED )
      { }

    /*!
      \brief execute action
      \param agent pointer to the agent itself
      \return true if action is performed. false if the ball is grounded
      (caller should fall back to the existing 2D kick primitives) or the
      kick could not be built.
    */
    bool execute( rcsc::PlayerAgent * agent );

    /*!
      \brief get the result ball position
      \return ball position after kick
     */
    const rcsc::Vector2D & ballResultPos() const
      {
          return M_ball_result_pos;
      }

    /*!
      \brief get the result ball velocity
      \return ball velocity after kick
     */
    const rcsc::Vector2D & ballResultVel() const
      {
          return M_ball_result_vel;
      }

};

#endif
