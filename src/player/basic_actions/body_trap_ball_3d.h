// -*-c++-*-

/*!
  \file body_trap_ball_3d.h
  \brief trap (deaden) an airborne ball using the v20 (stop_ball) command.
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

#ifndef RCSC_ACTION_BODY_TRAP_BALL_3D_H
#define RCSC_ACTION_BODY_TRAP_BALL_3D_H

#include <rcsc/player/soccer_action.h>

/*!
  \class Body_TrapBall3D
  \brief trap (immediately deaden) an airborne ball via the v20 `(stop_ball)`
  protocol command (librcsc's `PlayerAgent::doStopBall()`, see Step 6).

  NOTE: deliberately NOT named `Body_StopBall` -- that name is already taken
  by the existing, unrelated dash/kick-based ball-decelerating behavior
  (basic_actions/body_stop_ball.h), which only ever worked on a grounded ball
  and is left completely untouched by this class.

  execute() only issues the command when `wm.ball().posZ() > 0.0 &&
  wm.self().isKickable()`, mirroring the server's own silent-reject gate on
  `stop_ball` (rejected when the ball is not kickable) -- there is no point
  sending the command when the server will silently ignore it. Declines
  (`return false`) for the grounded-ball case, in which case a caller should
  use `Body_StopBall` (or an ordinary kick) instead.
*/
class Body_TrapBall3D
    : public rcsc::BodyAction {
private:

public:
    /*!
      \brief accessible from global.
    */
    Body_TrapBall3D()
      { }

    /*!
      \brief execute action
      \param agent pointer to the agent itself
      \return true if the (stop_ball) command was registered, false if
      declined (ball is grounded, or not currently kickable).
    */
    bool execute( rcsc::PlayerAgent * agent );

};

#endif
