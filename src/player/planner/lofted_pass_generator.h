// -*-c++-*-

/*!
  \file lofted_pass_generator.h
  \brief lofted (airborne) pass course generator Header File (v20 3D ball extension)
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

#ifndef LOFTED_PASS_GENERATOR_H
#define LOFTED_PASS_GENERATOR_H

#include "cooperative_action.h"

#include <rcsc/geom/vector_2d.h>
#include <rcsc/geom/vector_3d.h>
#include <rcsc/game_time.h>

#include <vector>

namespace rcsc {
class AbstractPlayerObject;
class WorldModel;
}

/*!
  \class LoftedPassGenerator
  \brief coarse, closed-form generator of one-step lofted (airborne) pass
  candidates for the ball holder, using the v20 3D ball extension.

  Deliberately much simpler than StrictCheckPassGenerator: it does not
  search over arbitrary receive points or run the full leading/through
  pass machinery. Instead it discretizes the search to a small, fixed
  grid -- 12 kick directions (0,30,...,330 deg) x 3 powers (50/80/100)
  x 3 loft angles (30/45/60 deg) = 108 candidate kicks per cycle -- and,
  for each one, simulates the resulting airborne ball trajectory in
  closed form (mirroring rcsc::InterceptSimulatorSelf3D's z(t) formula
  and BallObject's decaying xy inertia model) to find the first cycle at
  which the ball becomes reachable (height <= ServerParam::playerHeight())
  and, at that point, which player (teammate or opponent) is nearest.
  Candidates that would first become reachable by an opponent, or that
  leave the pitch before becoming reachable, are discarded; candidates
  reachable by a teammate become CooperativeAction::Pass instances
  (loft angle attached via CooperativeAction::loftAngle()).

  This is only meaningful once the ball is grounded and about to be
  kicked (i.e. is only registered as a first-action-of-chain generator,
  see ActGen_LoftedPass), and produces zero candidates whenever
  ServerParam::is2dMode() is true (every candidate's simulated z(t)
  never exceeds ServerParam::playerHeight(), so every candidate is
  "reachable" at cycle 0 by whichever player is already nearest --
  in practice this degenerates to duplicating a subset of
  StrictCheckPassGenerator's direct-pass candidates, which is harmless
  since ActionChainGraph's scoring simply treats them as one more
  option, but is not a useful use of cycle budget on such a server).
 */
class LoftedPassGenerator {
public:

    //! one (angle, power, loft) grid point tried per cycle
    static const int ANGLE_DIVS = 12; //!< 360/30
    static const int NUM_POWERS = 3;
    static const int NUM_LOFTS = 3;

    //! max cycles simulated per candidate before giving up on it
    static const int MAX_SIMULATION_STEP = 100;

private:

    rcsc::GameTime M_update_time;
    std::vector< CooperativeAction::Ptr > M_courses;

    // private for singleton
    LoftedPassGenerator();

    // not used
    LoftedPassGenerator( const LoftedPassGenerator & );
    LoftedPassGenerator & operator=( const LoftedPassGenerator & );

public:

    static
    LoftedPassGenerator & instance();

    void generate( const rcsc::WorldModel & wm );

    const std::vector< CooperativeAction::Ptr > & courses( const rcsc::WorldModel & wm )
      {
          generate( wm );
          return M_courses;
      }

private:

    void clear();

    void createCourses( const rcsc::WorldModel & wm );

    /*!
      \brief simulate one candidate (dir_deg, power, loft_deg) kick from the
      current ball position and find the first reachable receiver.
      \param wm world model
      \param dir_deg absolute kick direction (world angle, degrees)
      \param power kick power [0, ServerParam::maxPower()]
      \param loft_deg loft angle in degrees [0, 90]
      \return a new Pass candidate if a teammate is the first player able
              to reach the simulated trajectory, otherwise an empty Ptr.
     */
    CooperativeAction::Ptr simulateCandidate( const rcsc::WorldModel & wm,
                                              const double & dir_deg,
                                              const double & power,
                                              const double & loft_deg ) const;

    /*!
      \brief find the best player (teammate or opponent) to reach a ground
      point by simulation step \p step, among all players with a valid
      position. A player whose raw distance already exceeds step + 5.0
      cannot get there under any circumstance and is skipped outright;
      otherwise a simple turn+dash cycle estimate (mirroring
      StrictCheckPassGenerator's predictReceiverReachStep(), but without
      its penalty/pass-type tuning) is used to discard players who could
      not actually arrive by \p step, and the nearest of the remaining,
      reachable players is returned.
      \param wm world model
      \param pos ground point the ball is simulated to reach
      \param step the simulation cycle (from the ball-flight loop) at
             which \p pos is reached -- i.e. the deadline for reaching it
     */
    const rcsc::AbstractPlayerObject * nearestPlayer( const rcsc::WorldModel & wm,
                                                       const rcsc::Vector2D & pos,
                                                       const int step ) const;
};

#endif
