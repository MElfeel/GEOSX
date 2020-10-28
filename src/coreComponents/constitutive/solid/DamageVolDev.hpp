/*
 * ------------------------------------------------------------------------------------------------------------
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (c) 2018-2020 Lawrence Livermore National Security LLC
 * Copyright (c) 2018-2020 The Board of Trustees of the Leland Stanford Junior University
 * Copyright (c) 2018-2020 Total, S.A
 * Copyright (c) 2019-     GEOSX Contributors
 * All rights reserved
 *
 * See top level LICENSE, COPYRIGHT, CONTRIBUTORS, NOTICE, and ACKNOWLEDGEMENTS files for details.
 * ------------------------------------------------------------------------------------------------------------
 */


/**
 * @file Damage.hpp
 */

#ifndef GEOSX_CONSTITUTIVE_SOLID_DAMAGEVOLDEV_HPP_
#define GEOSX_CONSTITUTIVE_SOLID_DAMAGEVOLDEV_HPP_
#include "constitutive/solid/SolidBase.hpp"
#include "constitutive/solid/Damage.hpp"

namespace geosx
{
namespace constitutive
{

template< typename UPDATE_BASE >
class DamageVolDevUpdates : public DamageUpdates< UPDATE_BASE >
{
public:
  template< typename ... PARAMS >
  DamageVolDevUpdates( arrayView2d< real64 > const & inputDamage,
                       arrayView2d< real64 > const & inputStrainEnergyDensity,
                       real64 const & inputLengthScale,
                       real64 const & inputCriticalFractureEnergy,
                       real64 const & inputcriticalStrainEnergy,
                       PARAMS && ... baseParams ):
    DamageUpdates< UPDATE_BASE >( inputDamage, inputStrainEnergyDensity, inputLengthScale,
                                  inputCriticalFractureEnergy, inputcriticalStrainEnergy,
                                  std::forward< PARAMS >( baseParams )... )
  {}


  using DamageUpdates< UPDATE_BASE >::GetStiffness;
  using DamageUpdates< UPDATE_BASE >::SmallStrainNoState;
  using DamageUpdates< UPDATE_BASE >::SmallStrain;
  using DamageUpdates< UPDATE_BASE >::HypoElastic;
  using DamageUpdates< UPDATE_BASE >::HyperElastic;
  using DamageUpdates< UPDATE_BASE >::m_damage;
  using DamageUpdates< UPDATE_BASE >::m_strainEnergyDensity;
  using DamageUpdates< UPDATE_BASE >::m_criticalStrainEnergy;
  using DamageUpdates< UPDATE_BASE >::m_criticalFractureEnergy;
  using DamageUpdates< UPDATE_BASE >::m_lengthScale;


  //Lorentz type Degradation Function
  
  GEOSX_FORCE_INLINE
  GEOSX_HOST_DEVICE
  virtual real64 GetDegradationValue( localIndex const k,
                                      localIndex const q ) const override
  {
    //std::cout<<"Lorentz degradation"<<std::endl;
    //real64 m = 3*m_criticalFractureEnergy/(8*m_lengthScale*m_criticalStrainEnergy);
    real64 m = m_criticalFractureEnergy/(2*m_lengthScale*m_criticalStrainEnergy);
    real64 p = 1;
    return pow( 1 - m_damage( k, q ), 2 ) /( pow( 1 - m_damage( k, q ), 2 ) + m * m_damage( k, q ) * (1 + p*m_damage( k, q )) );
  }

  GEOSX_FORCE_INLINE
  GEOSX_HOST_DEVICE
  virtual real64 GetDegradationDerivative( real64 const d ) const override
  {
    //std::cout<<"Lorentz derivative"<<std::endl;
    real64 m = m_criticalFractureEnergy/(2*m_lengthScale*m_criticalStrainEnergy);
    real64 p = 1;
    return -m*(1 - d)*(1 + (2*p + 1)*d) / pow( pow( 1-d, 2 ) + m*d*(1+p*d), 2 );
  }

  GEOSX_FORCE_INLINE
  GEOSX_HOST_DEVICE
  virtual real64 GetDegradationSecondDerivative( real64 const d ) const override
  {
    //std::cout<<"Lorentz 2nd derivative"<<std::endl;
    real64 m = m_criticalFractureEnergy/(2*m_lengthScale*m_criticalStrainEnergy);
    real64 p = 1;
    return -2*m*( pow( d, 3 )*(2*m*p*p + m*p + 2*p + 1) + pow( d, 2 )*(-3*m*p*p -3*p) + d*(-3*m*p - 3) + (-m+p+2) )/pow( pow( 1-d, 2 ) + m*d*(1+p*d), 3 );
  }

  //Standard Quadratic Degradation Function

  // GEOSX_FORCE_INLINE
  // GEOSX_HOST_DEVICE
  // real64 GetDegradationValue( localIndex const k,
  //                             localIndex const q ) const override
  // {
  //   return (1 - m_damage( k, q ))*(1 - m_damage( k, q ));
  // }

  // GEOSX_FORCE_INLINE
  // GEOSX_HOST_DEVICE
  // real64 GetDegradationDerivative( real64 const d ) const override
  // {
  //   return -2*(1 - d);
  // }

  // GEOSX_FORCE_INLINE
  // GEOSX_HOST_DEVICE
  // real64 GetDegradationSecondDerivative( real64 const d ) const override
  // {
  //   return 2 * (d - d + 1);
  // }

  GEOSX_HOST_DEVICE inline
  virtual void GetStiffness( localIndex const k,
                             localIndex const q,
                             real64 (& c)[6][6] ) const override final
  {

    //Volumetric/Deviatoric Split
    UPDATE_BASE::GetStiffness( k, q, c );
    real64 const damageFactor = GetDegradationValue( k, q );
    real64 const K = UPDATE_BASE::getBulkModulus( k );
    real64 traceOfStress = this->m_stress( k, q, 0 ) + this->m_stress( k, q, 1 ) + this->m_stress( k, q, 2 );
    real64 compressionIndicator = 0;
    if( traceOfStress < 0.0 )
    {
      compressionIndicator = 1;
    }

    for( localIndex i=0; i<6; ++i )
    {
      for( localIndex j=0; j<6; ++j )
      {
        if( i < 3 && j < 3 )
        {
          c[i][j] = damageFactor*c[i][j] + (1 - damageFactor)*K*compressionIndicator;
        }
	else
	{
	  c[i][j]*=damageFactor;
        }
      }
    }

  }

  GEOSX_HOST_DEVICE
  virtual real64 calculateActiveStrainEnergyDensity( localIndex const k,
                                                     localIndex const q ) const override final
  {
    real64 const K = UPDATE_BASE::getBulkModulus( k );
    real64 traceOfStress = this->m_stress( k, q, 0 ) + this->m_stress( k, q, 1 ) + this->m_stress( k, q, 2 );
    real64 compressionIndicator = 0;
    if( traceOfStress < 0.0 )
    {
      compressionIndicator = 1;
    }
    real64 const sed = UPDATE_BASE::calculateStrainEnergyDensity( k, q ) - compressionIndicator*(traceOfStress/3.0)*(traceOfStress/3.0)/(2*K);
    //enforce irreversibility using history field for the strain energy density
    if( sed > m_strainEnergyDensity( k, q ) )
    {
      m_strainEnergyDensity( k, q ) = sed;
    }
    return m_strainEnergyDensity( k, q );
  }

  GEOSX_HOST_DEVICE
  virtual void getStress( localIndex const k,
                          localIndex const q,
                          real64 (& stress)[6] ) const override
  {

    //volumetric-deviatoric split
    real64 const damageFactor = GetDegradationValue( k, q );

    real64 traceOfStress = this->m_stress( k, q, 0 ) + this->m_stress( k, q, 1 ) + this->m_stress( k, q, 2 );
    real64 compressionIndicator = 0;
    if( traceOfStress < 0.0 )
    {
      compressionIndicator = 1;
    }

    stress[0] = this->m_stress( k, q, 0 ) * damageFactor + traceOfStress / 3.0 * (1 - damageFactor) * compressionIndicator;
    stress[1] = this->m_stress( k, q, 1 ) * damageFactor + traceOfStress / 3.0 * (1 - damageFactor) * compressionIndicator;
    stress[2] = this->m_stress( k, q, 2 ) * damageFactor + traceOfStress / 3.0 * (1 - damageFactor) * compressionIndicator;
    stress[3] = this->m_stress( k, q, 3 ) * damageFactor;
    stress[4] = this->m_stress( k, q, 4 ) * damageFactor;
    stress[5] = this->m_stress( k, q, 5 ) * damageFactor;
  }

  GEOSX_HOST_DEVICE
  real64 getEnergyThreshold() const override
  {
    return 3*m_criticalFractureEnergy/(16 * m_lengthScale);
  }

};

template< typename BASE >
class DamageVolDev : public Damage< BASE >
{
public:

  /// @typedef Alias for LinearElasticIsotropicUpdates
  using KernelWrapper = DamageVolDevUpdates< typename BASE::KernelWrapper >;

  using Damage< BASE >::m_damage;
  using Damage< BASE >::m_strainEnergyDensity;
  using Damage< BASE >::m_criticalFractureEnergy;
  using Damage< BASE >::m_lengthScale;
  using Damage< BASE >::m_criticalStrainEnergy;

  DamageVolDev( string const & name, dataRepository::Group * const parent );
  virtual ~DamageVolDev() override;


  static std::string CatalogName() { return string( "DamageVolDev" ) + BASE::m_catalogNameString; }
  virtual string getCatalogName() const override { return CatalogName(); }


  KernelWrapper createKernelUpdates()
  {
    return BASE::template createDerivedKernelUpdates< KernelWrapper >( m_damage.toView(),
                                                                       m_strainEnergyDensity.toView(),
                                                                       m_lengthScale,
                                                                       m_criticalFractureEnergy,
                                                                       m_criticalStrainEnergy );
  }

};


}
} /* namespace geosx */

#endif /* GEOSX_CONSTITUTIVE_SOLID_DAMAGEVOLDEV_HPP_ */