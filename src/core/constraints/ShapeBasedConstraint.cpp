#include <boost/mpi/collectives.hpp>

#include "ShapeBasedConstraint.hpp"
#include "communication.hpp"
#include "energy_inline.hpp"
#include "errorhandling.hpp"
#include "forces_inline.hpp"
#include "interaction_data.hpp"

namespace Constraints {

Vector3d ShapeBasedConstraint::total_force() const {
  Vector3d total_force;
  boost::mpi::all_reduce(comm_cart, m_local_force, total_force,
                         std::plus<Vector3d>());

  return total_force;
}

double ShapeBasedConstraint::min_dist() {
  double global_mindist = std::numeric_limits<double>::infinity();
  auto parts = local_cells.particles();

  auto const local_mindist = std::accumulate(
      parts.begin(), parts.end(), std::numeric_limits<double>::infinity(),
      [this](double min, Particle const &p) {
        IA_parameters *ia_params;
        ia_params = get_ia_param(p.p.type, m_type);
        if (checkIfInteraction(ia_params)) {
          double vec[3], dist;
          m_shape->calculate_dist(folded_position(p).data(), &dist, vec);
          return std::min(min, dist);
        } else
          return min;
      });
  boost::mpi::reduce(comm_cart, local_mindist, global_mindist,
                     boost::mpi::minimum<double>(), 0);
  return global_mindist;
}

void ShapeBasedConstraint::reflect_particle_from_flat_wall(Particle *p,
                                            const double *distance_vector,
                                            const double *folded_pos) const {
  double vec[3];
  double norm;

  memcpy(vec, distance_vector, 3 * sizeof(double));

  norm = sqrt(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
  if ((norm) > 0) {
    p->r.p[0] = p->r.p[0] - 2 * vec[0];
    p->r.p[1] = p->r.p[1] - 2 * vec[1];
    p->r.p[2] = p->r.p[2] - 2 * vec[2];

    /* vec seems to be the vector that points from the wall to the particle*/
    /* now normalize it */
    switch (std::abs(m_reflection_type)) {
    case (int)ReflectionType::NORMAL: {
      vec[0] /= norm;
      vec[1] /= norm;
      vec[2] /= norm;
      /* calculating scalar product */
      double normal_velocity =
          vec[0] * p->m.v[0] + vec[1] * p->m.v[1] + vec[2] * p->m.v[2];
      /* now add twice the normal component to the velcity */
      p->m.v[0] = p->m.v[0] - 2 * vec[0] * normal_velocity;
      p->m.v[1] = p->m.v[1] - 2 * vec[1] * normal_velocity;
      p->m.v[2] = p->m.v[2] - 2 * vec[2] * normal_velocity;
    } break;
    case (int)ReflectionType::NORMAL_TANGENTIAL:
      /* if bounce back, invert velocity */
      p->m.v[0] = -p->m.v[0];
      p->m.v[1] = -p->m.v[1];
      p->m.v[2] = -p->m.v[2];
      break;
    case (int)ReflectionType::NONE:
      break;
    }
  }
}

void ShapeBasedConstraint::add_force(Particle *p, double *folded_pos) {
  double dist, vec[3], force[3], torque1[3], torque2[3];
  Particle part_rep;
  part_rep.p.type = m_type;

  IA_parameters *ia_params = get_ia_param(p->p.type, part_rep.p.type);

  dist = 0.;
  for (int j = 0; j < 3; j++) {
    force[j] = 0;
#ifdef ROTATION
    torque1[j] = torque2[j] = 0;
#endif
  }

  if (checkIfInteraction(ia_params)) {
    m_shape->calculate_dist(folded_pos, &dist, vec);

    if (dist > 0) {
      calc_non_bonded_pair_force(p, &part_rep, ia_params, vec, dist,
                                 dist * dist, force, torque1, torque2);
#ifdef TUNABLE_SLIP
      if (tunable_slip) {
        add_tunable_slip_pair_force(p1, &constraints[n].part_rep, ia_params,
                                    vec, dist, force);
      }
#endif
    } else if (dist <= 0) {
      if (m_penetrable && !m_only_positive && (dist != 0.0)) {
        calc_non_bonded_pair_force(p, &part_rep, ia_params, vec, -1.0 * dist,
                                   dist * dist, force, torque1, torque2);
      }
      if (m_reflection_type != (int)ReflectionType::NONE) {
        reflect_particle_from_flat_wall(p, vec, folded_pos);
        dist=-dist;
      }
    } 
    if (dist<0 && !m_penetrable) {
        runtimeErrorMsg() << "Constraint"
                          << " violated by particle " << p->p.identity
                          << " dist " << dist;
    }
  }
  for (int j = 0; j < 3; j++) {
    p->f.f[j] += force[j];
    m_local_force[j] -= force[j];
#ifdef ROTATION
    p->f.torque[j] += torque1[j];
    part_rep.f.torque[j] += torque2[j];
#endif
  }
}

void ShapeBasedConstraint::add_energy(Particle *p, double *folded_pos,
                                      Observable_stat &energy) const {
  double dist, vec[3];
  IA_parameters *ia_params;
  double nonbonded_en = 0.0;
  Particle part_rep;
  part_rep.p.type = m_type;

  ia_params = get_ia_param(p->p.type, part_rep.p.type);

  dist = 0.;
  if (checkIfInteraction(ia_params)) {
    m_shape->calculate_dist(folded_pos, &dist, vec);
    if (dist > 0) {
      nonbonded_en = calc_non_bonded_pair_energy(p, &part_rep, ia_params, vec,
                                                 dist, dist * dist);
    } else if ((dist <= 0) && m_penetrable) {
      if (!m_only_positive && (dist < 0)) {
        nonbonded_en = calc_non_bonded_pair_energy(p, &part_rep, ia_params, vec,
                                                   -1.0 * dist, dist * dist);
      }
    } else {
      runtimeErrorMsg() << "Constraint "
                        << " violated by particle " << p->p.identity;
    }
  }
  if (part_rep.p.type >= 0)
    *obsstat_nonbonded(&energy, p->p.type, part_rep.p.type) += nonbonded_en;
}
}
