/*
 * ------------------------------------------------------------------------------------------------------------
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (c) 2018-2019 Lawrence Livermore National Security LLC
 * Copyright (c) 2018-2019 The Board of Trustees of the Leland Stanford Junior University
 * Copyright (c) 2018-2019 Total, S.A
 * Copyright (c) 2019-     GEOSX Contributors
 * All right reserved
 *
 * See top level LICENSE, COPYRIGHT, CONTRIBUTORS, NOTICE, and ACKNOWLEDGEMENTS files for details.
 * ------------------------------------------------------------------------------------------------------------
 */

/**
 * @file FlowSolverBase.hpp
 */

#ifndef GEOSX_PHYSICSSOLVERS_FINITEVOLUME_FLOWSOLVERBASE_HPP_
#define GEOSX_PHYSICSSOLVERS_FINITEVOLUME_FLOWSOLVERBASE_HPP_

#include "physicsSolvers/SolverBase.hpp"

namespace geosx
{

namespace dataRepository
{
class Group;
}
class FieldSpecificationBase;
class FiniteElementBase;
class DomainPartition;

/**
 * @class FlowSolverBase
 *
 * Base class for finite volume fluid flow solvers.
 * Provides some common features
 */
class FlowSolverBase : public SolverBase
{
public:
/**
   * @brief main constructor for Group Objects
   * @param name the name of this instantiation of Group in the repository
   * @param parent the parent group of this instantiation of Group
   */
  FlowSolverBase( const std::string& name,
                  Group * const parent );


  /// deleted default constructor
  FlowSolverBase() = delete;

  /// deleted copy constructor
  FlowSolverBase( FlowSolverBase const & ) = delete;

  /// default move constructor
  FlowSolverBase( FlowSolverBase && ) = default;

  /// deleted assignment operator
  FlowSolverBase & operator=( FlowSolverBase const & ) = delete;

  /// deleted move operator
  FlowSolverBase & operator=( FlowSolverBase && ) = delete;

  /**
   * @brief default destructor
   */
  virtual ~FlowSolverBase() override;

  virtual void RegisterDataOnMesh( Group * const MeshBodies ) override;
  
  void setPoroElasticCoupling() { m_poroElasticFlag = 1; }

  void setReservoirWellsCoupling() { m_coupledWellsFlag = 1; }

  virtual void CalculateAndApplyMassFlux( real64 const GEOSX_UNUSED_ARG( time_n ),
										  real64 const GEOSX_UNUSED_ARG( dt ),
										  DomainPartition * const GEOSX_UNUSED_ARG( domain )) {}

  virtual void UpdateEOS( real64 const GEOSX_UNUSED_ARG( time_n ),
						  real64 const GEOSX_UNUSED_ARG( dt ),
						  DomainPartition * const GEOSX_UNUSED_ARG( domain )) {}

  localIndex fluidIndex() const { return m_fluidIndex; }

  localIndex solidIndex() const { return m_solidIndex; }

  localIndex numDofPerCell() const { return m_numDofPerCell; }

  enum class timeIntegrationOption
  {
    SteadyState,
    ImplicitTransient,
    ExplicitTransient,
    InertialTransient
  };

  void SetTimeIntegrationOption( string const & stringVal )
  {
    if( stringVal == "ImplicitTransient" )
    {
      this->m_timeIntegrationOption = timeIntegrationOption::ImplicitTransient;
    }
    else if( stringVal == "ExplicitTransient" )
    {
      this->m_timeIntegrationOption = timeIntegrationOption::ExplicitTransient;
    }
    else if( stringVal == "InertialTransient" )
    {
      this->m_timeIntegrationOption = timeIntegrationOption::InertialTransient;
    }
    else if ( stringVal == "SteadyState" )
    {
      this->m_timeIntegrationOption = timeIntegrationOption::SteadyState;
    }
    else
    {
      GEOS_ERROR("Invalid time integration option for flow solver: " << stringVal);
    }
  }

  struct viewKeyStruct : SolverBase::viewKeyStruct
  {
    // input data
    static constexpr auto referencePorosityString = "referencePorosity";
    static constexpr auto permeabilityString      = "permeability";

    // gravity term precomputed values
    static constexpr auto gravityFlagString  = "gravityFlag";
    static constexpr auto gravityDepthString = "gravityDepth";

    // misc inputs
    static constexpr auto fluidNameString      = "fluidName";
    static constexpr auto solidNameString      = "solidName";
    static constexpr auto fluidIndexString     = "fluidIndex";
    static constexpr auto solidIndexString     = "solidIndex";

    static constexpr auto pressureString = "pressure";
    static constexpr auto deltaPressureString = "deltaPressure";
    static constexpr auto deltaVolumeString = "deltaVolume";

    static constexpr auto porosityString = "porosity";
    static constexpr auto porosityOldString = "porosityOld";

    static constexpr auto densityOldString = "densityOld";

    static constexpr auto totalCompressibilityString = "totalCompressibility";

    static constexpr auto aperture0String  = "aperture_n";

    static constexpr auto relaxationCoefficientString = "relaxationCoefficient";
    static constexpr auto injectionRelaxationCoefficientString = "injectionRelaxationCoefficient";
    static constexpr auto timeIntegrationOptionString = "timeIntegrationOption";
    static constexpr auto maximumApertureString      = "maximumAperture";

    using ViewKey = dataRepository::ViewKey;

    // input data
    ViewKey referencePorosity = { referencePorosityString };
    ViewKey permeability      = { permeabilityString };

    // gravity term precomputed values
    ViewKey gravityFlag  = { gravityFlagString };
    ViewKey gravityDepth = { gravityDepthString };

    // misc inputs
    ViewKey discretization = { discretizationString };
    ViewKey fluidName      = { fluidNameString };
    ViewKey solidName      = { solidNameString };
    ViewKey fluidIndex     = { fluidIndexString };
    ViewKey solidIndex     = { solidIndexString };
    ViewKey relaxationCoefficient = { relaxationCoefficientString };
    ViewKey injectionRelaxationCoefficient = { injectionRelaxationCoefficientString };
    ViewKey timeIntegrationOption = { timeIntegrationOptionString };
    ViewKey maximumAperture = { maximumApertureString };

  } viewKeysFlowSolverBase;

  struct groupKeyStruct : SolverBase::groupKeyStruct
  {
  } groupKeysFlowSolverBase;

  /**
   * @brief Setup stored views into domain data for the current step
   */
  virtual void ResetViews( DomainPartition * const domain );

private:

  /**
   * @brief This function generates various discretization information for later use.
   * @param domain the domain parition
   */
  void PrecomputeData(DomainPartition *const domain);


protected:

  virtual void InitializePreSubGroups(Group * const rootGroup) override;

  virtual void InitializePostInitialConditions_PreSubGroups(Group * const rootGroup) override;


  /// flag to determine whether or not to apply gravity
  integer m_gravityFlag;

  /// name of the fluid constitutive model
  string m_fluidName;

  /// name of the solid constitutive model
  string m_solidName;

  /// index of the fluid constitutive model
  localIndex m_fluidIndex;

  /// index of the solid constitutive model
  localIndex m_solidIndex;

  /// flag to determine whether or not coupled with solid solver
  integer m_poroElasticFlag;

  /// flag to determine whether or not coupled with wells
  integer m_coupledWellsFlag;
  
  /// the number of Degrees of Freedom per cell
  localIndex m_numDofPerCell;

  /// the relaxation Coefficient for mass flux
  real64 m_relaxationCoefficient;

  /// the relaxation Coefficient for injection BC
  real64 m_injectionRelaxationCoefficient;

  /// option for time integration
  string m_timeIntegrationOptionString;
  timeIntegrationOption m_timeIntegrationOption;

  /// views into constant data fields
  ElementRegionManager::ElementViewAccessor<arrayView1d<integer>> m_elemGhostRank;
  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_volume;
  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_gravDepth;
  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_porosityRef;

  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_elementArea;
  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_elementAperture0;
  ElementRegionManager::ElementViewAccessor<arrayView1d<real64>>  m_elementAperture;

};

}

#endif //GEOSX_PHYSICSSOLVERS_FINITEVOLUME_FLOWSOLVERBASE_HPP_