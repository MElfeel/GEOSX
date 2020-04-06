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
 * @file TwoPhaseBase.cpp
 */

#include "TwoPhaseBase.hpp"

#include "mpiCommunications/CommunicationTools.hpp"
#include "mpiCommunications/NeighborCommunicator.hpp"
#include "managers/FieldSpecification/FieldSpecificationManager.hpp"
#include "common/DataTypes.hpp"
#include "common/TimingMacros.hpp"
#include "constitutive/ConstitutiveManager.hpp"
#include "finiteVolume/FiniteVolumeManager.hpp"
#include "finiteVolume/FluxApproximationBase.hpp"
#include "managers/DomainPartition.hpp"
#include "managers/NumericalMethodsManager.hpp"
#include "mesh/MeshForLoopInterface.hpp"
#include "constitutive/fluid/MultiFluidBase.hpp"
#include "constitutive/relativePermeability/RelativePermeabilityBase.hpp"
#include "physicsSolvers/fluidFlow/TwoPhaseBaseKernels.hpp"

/**
 * @namespace the geosx namespace that encapsulates the majority of the code
 */
namespace geosx
{

using namespace dataRepository;
using namespace constitutive;
using namespace TwoPhaseBaseKernels;


TwoPhaseBase::TwoPhaseBase( const std::string & name,
                            Group * const parent ):
  FlowSolverBase( name, parent )
{
  m_numDofPerCell = 2;

  this->registerWrapper( viewKeyStruct::relPermNameString, &m_relPermName, false )->
    setInputFlag( InputFlags::REQUIRED )->
    setDescription( "Name of the relative permeability constitutive model to use" );

  this->registerWrapper( viewKeyStruct::relPermIndexString, &m_relPermIndex, false );

}

void TwoPhaseBase::RegisterDataOnMesh( Group * const MeshBodies )
{
  FlowSolverBase::RegisterDataOnMesh( MeshBodies );

  for( auto & mesh : MeshBodies->GetSubGroups() )
  {
    MeshLevel * meshLevel = Group::group_cast< MeshBody * >( mesh.second )->getMeshLevel( 0 );

    ElementRegionManager * const elemManager = meshLevel->getElemManager();

    elemManager->forElementSubRegions< CellElementSubRegion >( [&]( CellElementSubRegion & subRegion )
    {
      // non-wetting phase pressure
      subRegion.registerWrapper< array1d< real64 > >( viewKeyStruct::pressureString )->setPlotLevel( PlotLevel::LEVEL_0 );
      subRegion.registerWrapper< array1d< real64 > >( viewKeyStruct::deltaPressureString );

      // phase saturation
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::phaseSatString )->setPlotLevel( PlotLevel::LEVEL_0 );
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::newPhaseSatString );
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::deltaPhaseSatString );

      // auxiliary variables
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::phaseMobilityString );
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dPressureString );
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dSaturationString );

      // backup fields
      subRegion.registerWrapper< array1d< real64 > >( viewKeyStruct::porosityOldString );
      subRegion.registerWrapper< array2d< real64 > >( viewKeyStruct::phaseDensityOldString );

    } );
  }
}

void TwoPhaseBase::UpdateFluidModel( Group * const dataGroup ) const
{
  GEOSX_MARK_FUNCTION;

  MultiFluidBase * const fluid = GetConstitutiveModel< MultiFluidBase >( dataGroup, m_fluidName );

  arrayView1d< real64 const > const & pres =
    dataGroup->getReference< array1d< real64 > >( viewKeyStruct::pressureString );
  arrayView1d< real64 const > const & dPres =
    dataGroup->getReference< array1d< real64 > >( viewKeyStruct::deltaPressureString );

  real64 const dummyTemperature = 293.15;
  stackArray1d< real64, 2 > dummyCompFrac( 2 );
  dummyCompFrac = 0;

  forAll< serialPolicy >( dataGroup->size(), [=] ( localIndex const a )
  {
    fluid->PointUpdate( pres[a] + dPres[a], dummyTemperature, dummyCompFrac, a, 0 );
  } );

}

void TwoPhaseBase::UpdateSolidModel( Group * const dataGroup ) const
{
  GEOSX_MARK_FUNCTION;

  ConstitutiveBase * const solid = GetConstitutiveModel< ConstitutiveBase >( dataGroup, m_solidName );

  arrayView1d< real64 const > const & pres =
    dataGroup->getReference< array1d< real64 > >( viewKeyStruct::pressureString );
  arrayView1d< real64 const > const & dPres =
    dataGroup->getReference< array1d< real64 > >( viewKeyStruct::deltaPressureString );

  forAll< serialPolicy >( dataGroup->size(), [=] ( localIndex const a )
  {
    solid->StateUpdatePointPressure( pres[a] + dPres[a], a, 0 );
  } );
}

void TwoPhaseBase::UpdateRelPermModel( Group * const dataGroup ) const
{
  GEOSX_MARK_FUNCTION;

  RelativePermeabilityBase * const relPerm = GetConstitutiveModel< RelativePermeabilityBase >( dataGroup, m_relPermName );

  arrayView2d< real64 const > const & phaseSat =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::phaseSatString );
  arrayView2d< real64 const > const & dPhaseSat =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::deltaPhaseSatString );

  // ultimately, we may want to switch to old sat / new sat in the solvers as it seems more convenient
  arrayView2d< real64 > const & newPhaseSat =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::newPhaseSatString );

  localIndex constexpr numPhases = NUM_PHASES;

  forAll< serialPolicy >( dataGroup->size(), [=] ( localIndex const a )
  {
    for( localIndex ip = 0; ip < numPhases; ++ip )
    {
      newPhaseSat[a][ip]  = phaseSat[a][ip] + dPhaseSat[a][ip];
    }
  } );

  relPerm->BatchUpdate( newPhaseSat );
}

void TwoPhaseBase::UpdatePhaseMobility( Group * const dataGroup ) const
{
  GEOSX_MARK_FUNCTION;

  MultiFluidBase * const fluid = GetConstitutiveModel< MultiFluidBase >( dataGroup, m_fluidName );

  RelativePermeabilityBase * const relPerm = GetConstitutiveModel< RelativePermeabilityBase >( dataGroup, m_relPermName );

  // phase relperms
  arrayView3d< real64 const > const & phaseRelPerm =
    relPerm->getReference< array3d< real64 > >( RelativePermeabilityBase::viewKeyStruct::phaseRelPermString );
  arrayView4d< real64 const > const & dPhaseRelPerm_dSat =
    relPerm->getReference< array4d< real64 > >( RelativePermeabilityBase::viewKeyStruct::dPhaseRelPerm_dPhaseVolFractionString );

  // phase densities
  arrayView3d< real64 const > const & phaseDens =
    fluid->getReference< array3d< real64 > >( MultiFluidBase::viewKeyStruct::phaseDensityString );
  arrayView3d< real64 const > const & dPhaseDens_dPres =
    fluid->getReference< array3d< real64 > >( MultiFluidBase::viewKeyStruct::dPhaseDensity_dPressureString );

  // phase viscosities
  arrayView3d< real64 const > const & phaseVisc =
    fluid->getReference< array3d< real64 > >( MultiFluidBase::viewKeyStruct::phaseViscosityString );
  arrayView3d< real64 const > const & dPhaseVisc_dPres =
    fluid->getReference< array3d< real64 > >( MultiFluidBase::viewKeyStruct::dPhaseViscosity_dPressureString );

  // phase mobilities
  arrayView2d< real64 > const & phaseMob =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::phaseMobilityString );
  arrayView2d< real64 > const & dPhaseMob_dPres =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dPressureString );
  arrayView2d< real64 > const & dPhaseMob_dSat =
    dataGroup->getReference< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dSaturationString );

  PhaseMobilityKernel::Launch( dataGroup->size(),
                               phaseDens,
                               dPhaseDens_dPres,
                               phaseVisc,
                               dPhaseVisc_dPres,
                               phaseRelPerm,
                               dPhaseRelPerm_dSat,
                               phaseMob,
                               dPhaseMob_dPres,
                               dPhaseMob_dSat );
}

void TwoPhaseBase::UpdateState( Group * dataGroup ) const
{
  GEOSX_MARK_FUNCTION;

  UpdateFluidModel( dataGroup );
  UpdateSolidModel( dataGroup );
  UpdateRelPermModel( dataGroup );
  UpdatePhaseMobility( dataGroup );
}

void TwoPhaseBase::PostProcessInput()
{
  FlowSolverBase::PostProcessInput();
}

void TwoPhaseBase::InitializePreSubGroups( Group * const rootGroup )
{
  FlowSolverBase::InitializePreSubGroups( rootGroup );

  DomainPartition * const domain = rootGroup->GetGroup< DomainPartition >( keys::domain );
  ConstitutiveManager const * const cm = domain->getConstitutiveManager();

  MultiFluidBase const * fluid = cm->GetConstitutiveRelation< MultiFluidBase >( m_fluidName );

  RelativePermeabilityBase const * relPerm = cm->GetConstitutiveRelation< RelativePermeabilityBase >( m_relPermName );
  GEOSX_ERROR_IF( relPerm == nullptr, "Relative permeability model " + m_relPermName + " not found" );
  m_relPermIndex = relPerm->getIndexInParent();

  GEOSX_ERROR_IF( fluid->numFluidPhases() != 2,
                  "Invalid number of fluid phases in fluid model '" << m_fluidName << "'" );

  // Consistency check between the models
  GEOSX_ERROR_IF( fluid->numFluidPhases() != relPerm->numFluidPhases(),
                  "Number of fluid phases differs between fluid model '" << m_fluidName
                                                                         << "' and relperm model '" << m_relPermName << "'" );

  localIndex constexpr numPhases  = NUM_PHASES;
  GEOSX_ERROR_IF( fluid->numFluidPhases() != numPhases,
                  "Number of fluid phases differs between fluid model '" << m_fluidName
                                                                         << "' and relperm model '" << m_relPermName << "'" );

  for( localIndex ip = 0; ip < numPhases; ++ip )
  {
    string const & phase_fl = fluid->phaseName( ip );
    string const & phase_rp = relPerm->phaseName( ip );
    GEOSX_ERROR_IF( phase_fl != phase_rp, "Phase '" << phase_fl << "' in fluid model '"   << m_fluidName
                                                    << "' does not match phase '"   << phase_rp << "' in relperm model '" << m_relPermName << "'" );
  }

  localIndex wettingPhaseSat = -1;
  localIndex nonWettingPhaseSat = -1;
  // determine the indices of the wetting and non-wetting phases
  if( (fluid->phaseName( 0 ) == "oil" && fluid->phaseName( 1 ) == "gas") ||
      (fluid->phaseName( 1 ) == "oil" && fluid->phaseName( 0 ) == "water") )
  {
    wettingPhaseSat    = 0;
    nonWettingPhaseSat = 1;
  }
  else if( (fluid->phaseName( 1 ) == "oil" && fluid->phaseName( 0 ) == "gas") ||
           (fluid->phaseName( 0 ) == "oil" && fluid->phaseName( 1 ) == "water") )
  {
    wettingPhaseSat    = 1;
    nonWettingPhaseSat = 0;
  }
  GEOSX_ERROR_IF( wettingPhaseSat == -1 || nonWettingPhaseSat == -1,
                  "TwoPhaseBase: the accepted phase names are water, oil, and gas" );

  // fill the array mapping the phase index to the row offset in the residual
  m_phaseToRow.resize( NUM_PHASES );
  m_phaseToRow[wettingPhaseSat] = RowOffset::WETTING;
  m_phaseToRow[nonWettingPhaseSat] = RowOffset::NONWETTING;

  for( auto & mesh : domain->getMeshBodies()->GetSubGroups() )
  {
    MeshLevel * meshLevel = Group::group_cast< MeshBody * >( mesh.second )->getMeshLevel( 0 );
    ResizeFields( meshLevel );
  }
}

void TwoPhaseBase::ResizeFields( MeshLevel * const meshLevel )
{
  localIndex constexpr numPhases = NUM_PHASES;

  applyToSubRegions( meshLevel, [&] ( ElementSubRegionBase & subRegion )
  {
    subRegion.getReference< array2d< real64 > >( viewKeyStruct::phaseSatString ).resizeDimension< 1 >( numPhases );
    subRegion.getReference< array2d< real64 > >( viewKeyStruct::newPhaseSatString ).resizeDimension< 1 >( numPhases );
    subRegion.getReference< array2d< real64 > >( viewKeyStruct::deltaPhaseSatString ).resizeDimension< 1 >( numPhases );

    subRegion.getReference< array2d< real64 > >( viewKeyStruct::phaseMobilityString ).resizeDimension< 1 >( numPhases );
    subRegion.getReference< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dPressureString ).resizeDimension< 1 >( numPhases );
    subRegion.getReference< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dSaturationString ).resizeDimension< 1 >( numPhases );

    subRegion.getReference< array2d< real64 > >( viewKeyStruct::phaseDensityOldString ).resizeDimension< 1 >( numPhases );

  } );
}

void TwoPhaseBase::InitializePostInitialConditions_PreSubGroups( Group * const rootGroup )
{
  GEOSX_MARK_FUNCTION;

  FlowSolverBase::InitializePostInitialConditions_PreSubGroups( rootGroup );

  DomainPartition * const domain = rootGroup->GetGroup< DomainPartition >( keys::domain );

  MeshLevel * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );

  //TODO this is a hack until the sets are fixed to include ghosts!!
  std::map< string, string_array > fieldNames;
  fieldNames["elems"].push_back( viewKeyStruct::pressureString );
  fieldNames["elems"].push_back( viewKeyStruct::phaseSatString );

  CommunicationTools::SynchronizeFields( fieldNames, mesh, domain->getNeighbors() );

  ConstitutiveManager * const constitutiveManager = domain->getConstitutiveManager();

  // TODO find a way to set this before constitutive model is duplicated and attached to subregions?
  {
    MultiFluidBase * const fluid = constitutiveManager->GetConstitutiveRelation< MultiFluidBase >( m_fluidName );
    fluid->setMassFlag( static_cast< bool >(true) );
  }

  // set mass fraction flag on subregion models
  applyToSubRegions( mesh, [&] ( ElementSubRegionBase & subRegion )
  {
    MultiFluidBase * const fluid = GetConstitutiveModel< MultiFluidBase >( &subRegion, m_fluidName );
    fluid->setMassFlag( static_cast< bool >(true) );
  } );

  // initialize primary variables from applied initial conditions
  ResetViews( domain );

  // initialize fluid state
  applyToSubRegions( mesh, [&] ( ElementSubRegionBase & subRegion )
  {
    UpdateState( &subRegion );
  } );
}

real64 TwoPhaseBase::SolverStep( real64 const & time_n,
                                 real64 const & dt,
                                 const int cycleNumber,
                                 DomainPartition * domain )
{
  GEOSX_MARK_FUNCTION;

  real64 dt_return;

  ImplicitStepSetup( time_n, dt, domain, m_dofManager, m_matrix, m_rhs, m_solution );

  // currently the only method is implicit time integration
  dt_return = this->NonlinearImplicitStep( time_n, dt, cycleNumber, domain, m_dofManager, m_matrix, m_rhs, m_solution );

  // final step for completion of timestep. typically secondary variable updates and cleanup.
  ImplicitStepComplete( time_n, dt_return, domain );

  return dt_return;
}


void TwoPhaseBase::ImplicitStepSetup( real64 const & GEOSX_UNUSED_PARAM( time_n ),
                                      real64 const & GEOSX_UNUSED_PARAM( dt ),
                                      DomainPartition * const domain,
                                      DofManager & dofManager,
                                      ParallelMatrix & matrix,
                                      ParallelVector & rhs,
                                      ParallelVector & solution )
{
  // bind the stored views to the current domain
  ResetViews( domain );

  // set deltas to zero and recompute dependent quantities
  ResetStateToBeginningOfStep( domain );

  // backup fields used in time derivative approximation
  BackupFields( domain );

  if( !m_coupledWellsFlag )
  {
    SetupSystem( domain, dofManager, matrix, rhs, solution );
  }

}

void TwoPhaseBase::ImplicitStepComplete( real64 const & GEOSX_UNUSED_PARAM( time_n ),
                                         real64 const & GEOSX_UNUSED_PARAM( dt ),
                                         DomainPartition * const domain )
{
  MeshLevel * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );

  applyToSubRegionsComplete( mesh, [&] ( localIndex const er, localIndex const esr,
                                         ElementRegionBase &,
                                         ElementSubRegionBase & subRegion )
  {
    arrayView1d< real64 const > const & dPres = m_deltaPressure[er][esr];
    arrayView2d< real64 const > const & dPhaseSat = m_deltaPhaseSat[er][esr];

    arrayView1d< real64 > const & pres = m_pressure[er][esr];
    arrayView2d< real64 > const & phaseSat = m_phaseSat[er][esr];

    forAll< serialPolicy >( subRegion.size(), [=] ( localIndex const ei )
    {
      pres[ei] += dPres[ei];
      phaseSat[ei][0] += dPhaseSat[ei][0];
      phaseSat[ei][1] += dPhaseSat[ei][1];
    } );
  } );
}


void TwoPhaseBase::AssembleSystem( real64 const time_n,
                                   real64 const dt,
                                   DomainPartition * const domain,
                                   DofManager const & dofManager,
                                   ParallelMatrix & matrix,
                                   ParallelVector & rhs )
{
  GEOSX_MARK_FUNCTION;

  matrix.open();
  rhs.open();

  AssembleAccumulationTerms( domain, &dofManager, &matrix, &rhs );

  AssembleFluxTerms( time_n, dt, domain, &dofManager, &matrix, &rhs );

  matrix.close();
  rhs.close();

  if( getLogLevel() == 2 )
  {
    GEOSX_LOG_RANK_0( "After TwoPhaseBase::AssembleSystem" );
    GEOSX_LOG_RANK_0( "\nJacobian:\n" );
    std::cout << matrix;
    GEOSX_LOG_RANK_0( "\nResidual:\n" );
    std::cout << rhs;
  }

  if( getLogLevel() >= 3 )
  {
    integer newtonIter = m_nonlinearSolverParameters.m_numNewtonIterations;

    string filename_mat = "matrix_" + std::to_string( time_n ) + "_" + std::to_string( newtonIter ) + ".mtx";
    matrix.write( filename_mat, LAIOutputFormat::MATRIX_MARKET );

    string filename_rhs = "rhs_" + std::to_string( time_n ) + "_" + std::to_string( newtonIter ) + ".mtx";
    rhs.write( filename_rhs, LAIOutputFormat::MATRIX_MARKET );

    GEOSX_LOG_RANK_0( "After TwoPhaseBase::AssembleSystem" );
    GEOSX_LOG_RANK_0( "Jacobian: written to " << filename_mat );
    GEOSX_LOG_RANK_0( "Residual: written to " << filename_rhs );
  }

}

void TwoPhaseBase::AssembleAccumulationTerms( DomainPartition const * const domain,
                                              DofManager const * const dofManager,
                                              ParallelMatrix * const matrix,
                                              ParallelVector * const rhs )
{
  GEOSX_MARK_FUNCTION;

  MeshLevel const * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );

  localIndex constexpr numDof = NUM_DOF;
  localIndex constexpr numPhases = NUM_PHASES;

  string const dofKey = dofManager->getKey( viewKeyStruct::elemDofFieldString );

  applyToSubRegionsComplete( mesh, [&] ( localIndex const er, localIndex const esr,
                                         ElementRegionBase const &,
                                         ElementSubRegionBase const & subRegion )
  {
    arrayView1d< globalIndex const > const & dofNumber = subRegion.getReference< array1d< globalIndex > >( dofKey );

    arrayView1d< integer const > const & elemGhostRank = m_elemGhostRank[er][esr];

    arrayView1d< real64 const > const & volume           = m_volume[er][esr];
    arrayView1d< real64 const > const & porosityRef      = m_porosityRef[er][esr];
    arrayView2d< real64 const > const & pvMult           = m_pvMult[er][esr][m_solidIndex];
    arrayView2d< real64 const > const & dPvMult_dPres    = m_dPvMult_dPres[er][esr][m_solidIndex];

    arrayView2d< real64 const > const & phaseSat         = m_phaseSat[er][esr];
    arrayView2d< real64 const > const & dPhaseSat        = m_deltaPhaseSat[er][esr];

    arrayView3d< real64 const > const & phaseDens        = m_phaseDens[er][esr][m_fluidIndex];
    arrayView3d< real64 const > const & dPhaseDens_dPres = m_dPhaseDens_dPres[er][esr][m_fluidIndex];

    arrayView1d< real64 const > const & porosityOld      = m_porosityOld[er][esr];
    arrayView2d< real64 const > const & phaseDensOld     = m_phaseDensOld[er][esr];

    localIndex const dp = ColOffset::DPRES;
    localIndex const dS = ColOffset::DSAT;

    forAll< serialPolicy >( subRegion.size(), [=] ( localIndex const ei )
    {
      if( elemGhostRank[ei] < 0 )
      {

        stackArray1d< globalIndex, numPhases >     eqnRowIndices( numPhases );
        stackArray1d< globalIndex, numDof >        dofColIndices( numDof );
        stackArray1d< real64, numPhases >          localAccum( numPhases );
        stackArray2d< real64, numPhases * numDof > localAccumJacobian( numPhases, numDof );

        AccumulationKernel::Compute( volume[ei],
                                     porosityOld[ei],
                                     porosityRef[ei],
                                     pvMult[ei][0],
                                     dPvMult_dPres[ei][0],
                                     phaseSat[ei],
                                     dPhaseSat[ei],
                                     phaseDensOld[ei],
                                     phaseDens[ei][0],
                                     dPhaseDens_dPres[ei][0],
                                     localAccum,
                                     localAccumJacobian );

        // dof numbers
        dofColIndices[dp] = dofNumber[ei] + dp;
        dofColIndices[dS] = dofNumber[ei] + dS;

        for( localIndex ip = 0; ip < numPhases; ++ip )
        {
          eqnRowIndices[ip] = dofNumber[ei] + m_phaseToRow[ip];
        }

        // add contribution to global residual and jacobian

        rhs->add( eqnRowIndices,
                  localAccum );

        matrix->add( eqnRowIndices,
                     dofColIndices,
                     localAccumJacobian );

      }
    } );
  } );

}


bool TwoPhaseBase::CheckSystemSolution( DomainPartition const * const domain,
                                        DofManager const & dofManager,
                                        ParallelVector const & solution,
                                        real64 const scalingFactor )
{
  MeshLevel const * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );
  real64 const * localSolution = solution.extractLocalVector();
  int localCheck = 1;

  string const elemDofKey = dofManager.getKey( viewKeyStruct::elemDofFieldString );

  applyToSubRegionsComplete( mesh, [&] ( localIndex const er, localIndex const esr,
                                         ElementRegionBase const &,
                                         ElementSubRegionBase const & subRegion )
  {
    arrayView1d< globalIndex const > const & elemDofNumber =
      subRegion.getReference< array1d< globalIndex > >( elemDofKey );

    arrayView1d< integer const > const & elemGhostRank = m_elemGhostRank[er][esr];

    arrayView1d< real64 const > const & pres  = m_pressure[er][esr];
    arrayView1d< real64 const > const & dPres = m_deltaPressure[er][esr];
    arrayView2d< real64 const > const & phaseSat  = m_phaseSat[er][esr];
    arrayView2d< real64 const > const & dPhaseSat = m_deltaPhaseSat[er][esr];

    forAll< serialPolicy >( subRegion.size(), [&] ( localIndex const ei )
    {
      if( elemGhostRank[ei] >= 0 )
      {
        return;
      }

      // extract solution and apply to dP
      {
        localIndex const lid = solution.getLocalRowID( elemDofNumber[ei] + ColOffset::DPRES );
        real64 const newPres = pres[ei] + dPres[ei] + scalingFactor * localSolution[lid];
        if( newPres < 0.0 )
        {
          localCheck = 0;
        }
      }

      // extract solution and apply to dS
      {
        localIndex const lid = solution.getLocalRowID( elemDofNumber[ei] + ColOffset::DSAT );
        real64 const newPhaseSat  = phaseSat[ei][0] + dPhaseSat[ei][0] + scalingFactor * localSolution[lid];
        if( newPhaseSat < 0.0 || newPhaseSat > 1.0 )
        {
          localCheck = 0;
        }
      }
    } );
  } );

  int const globalCheck = MpiWrapper::Min( localCheck );

  return globalCheck;
}


void TwoPhaseBase::SolveSystem( DofManager const & dofManager,
                                ParallelMatrix & matrix,
                                ParallelVector & rhs,
                                ParallelVector & solution )
{
  GEOSX_MARK_FUNCTION;

  rhs.scale( -1.0 );
  solution.zero();

  SolverBase::SolveSystem( dofManager, matrix, rhs, solution );

}

void TwoPhaseBase::ResetStateToBeginningOfStep( DomainPartition * const domain )
{
  MeshLevel * mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );

  applyToSubRegionsComplete( mesh, [&] ( localIndex er, localIndex esr,
                                         ElementRegionBase &,
                                         ElementSubRegionBase & subRegion )
  {
    arrayView1d< real64 > const & dPres = m_deltaPressure[er][esr];
    arrayView2d< real64 > const & dPhaseSat  = m_deltaPhaseSat[er][esr];

    forAll< serialPolicy >( subRegion.size(), [=] ( localIndex const ei )
    {
      dPres[ei]   = 0.0;
      dPhaseSat[ei][0] = 0.0;
      dPhaseSat[ei][1] = 0.0;
    } );

    UpdateState( &subRegion );
  } );
}


void TwoPhaseBase::ResetViews( DomainPartition * const domain )
{
  FlowSolverBase::ResetViews( domain );

  MeshLevel * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );
  ElementRegionManager * const elemManager = mesh->getElemManager();
  ConstitutiveManager * const constitutiveManager = domain->getConstitutiveManager();

  // primary variables
  m_pressure =
    elemManager->ConstructViewAccessor< array1d< real64 >, arrayView1d< real64 > >( viewKeyStruct::pressureString );
  m_deltaPressure =
    elemManager->ConstructViewAccessor< array1d< real64 >, arrayView1d< real64 > >( viewKeyStruct::deltaPressureString );
  m_phaseSat =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::phaseSatString );
  m_deltaPhaseSat =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::deltaPhaseSatString );

  // auxiliary data
  m_phaseMob =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::phaseMobilityString );
  m_dPhaseMob_dPres =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::dPhaseMobility_dPressureString );
  m_dPhaseMob_dSat =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::dPhaseMobility_dSaturationString );

  m_pvMult =
    elemManager->ConstructFullMaterialViewAccessor< array2d< real64 >, arrayView2d< real64 > >(
      ConstitutiveBase::viewKeyStruct::poreVolumeMultiplierString,
      constitutiveManager );
  m_dPvMult_dPres =
    elemManager->ConstructFullMaterialViewAccessor< array2d< real64 >, arrayView2d< real64 > >(
      ConstitutiveBase::viewKeyStruct::dPVMult_dPresString,
      constitutiveManager );

  m_phaseDens =
    elemManager->ConstructFullMaterialViewAccessor< array3d< real64 >, arrayView3d< real64 > >(
      MultiFluidBase::viewKeyStruct::phaseDensityString,
      constitutiveManager );
  m_dPhaseDens_dPres =
    elemManager->ConstructFullMaterialViewAccessor< array3d< real64 >, arrayView3d< real64 > >(
      MultiFluidBase::viewKeyStruct::dPhaseDensity_dPressureString,
      constitutiveManager );

  // backup data
  m_porosityOld =
    elemManager->ConstructViewAccessor< array1d< real64 >, arrayView1d< real64 > >( viewKeyStruct::porosityOldString );
  m_phaseDensOld =
    elemManager->ConstructViewAccessor< array2d< real64 >, arrayView2d< real64 > >( viewKeyStruct::phaseDensityOldString );

}

void TwoPhaseBase::BackupFields( DomainPartition * const domain )
{
  MeshLevel * const mesh = domain->getMeshBody( 0 )->getMeshLevel( 0 );

  localIndex constexpr numPhases  = NUM_PHASES;

  // backup some fields used in time derivative approximation
  applyToSubRegionsComplete( mesh, [&] ( localIndex const er, localIndex const esr,
                                         ElementRegionBase &,
                                         ElementSubRegionBase & subRegion )
  {
    arrayView1d< integer const > const & elemGhostRank = m_elemGhostRank[er][esr];

    arrayView1d< real64 const > const & poroRef   = m_porosityRef[er][esr];
    arrayView1d< real64 > const & poroOld         = m_porosityOld[er][esr];
    arrayView2d< real64 const > const & pvMult    = m_pvMult[er][esr][m_solidIndex];

    arrayView3d< real64 const > const & phaseDens = m_phaseDens[er][esr][m_fluidIndex];
    arrayView2d< real64 > const & phaseDensOld    = m_phaseDensOld[er][esr];

    forAll< serialPolicy >( subRegion.size(), [=] ( localIndex const ei )
    {
      if( elemGhostRank[ei] < 0 )
      {
        poroOld[ei] = poroRef[ei] * pvMult[ei][0];
        for( localIndex ip = 0; ip < numPhases; ++ip )
        {
          phaseDensOld[ei][ip] = phaseDens[ei][0][ip];
        }
      }
    } );
  } );
}

} /* namespace geosx */
