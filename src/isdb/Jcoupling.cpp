/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   Copyright (c) 2016,2017 The plumed team
   (see the PEOPLE file at the root of the distribution for a list of names)

   See http://www.plumed.org for more information.

   This file is part of plumed, version 2.

   plumed is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   plumed is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with plumed.  If not, see <http://www.gnu.org/licenses/>.
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#include "MetainferenceBase.h"
#include "core/ActionRegister.h"
#include "core/PlumedMain.h"
#include "tools/OpenMP.h"
#include "tools/Pbc.h"
#include "tools/Torsion.h"

#include <string>
#include <cmath>

using namespace std;

namespace PLMD {
namespace isdb {

//+PLUMEDOC ISDB_COLVAR JCOUPLING
/*
Calculates \f$^3J\f$ coupling constants for a dihedral angle.

The J-coupling between two atoms is given by the Karplus relation:

\f[
^3J(\theta)=A\cos^2(\theta+\Delta\theta)+B\cos(\theta+\Delta\theta)+C
\f]

where \f$A\f$, \f$B\f$ and \f$C\f$ are the Karplus parameters and \f$\Delta\theta\f$ is an additional constant
added on to the dihedral angle \f$\theta\f$. The Karplus parameters are determined empirically and are dependent
on the type of J-coupling.

This collective variable computes the J-couplings for a set of atoms defining a dihedral angle. You can specify
the atoms involved using the \ref MOLINFO notation. You can also specify the experimental couplings using the
ADDCOUPLINGS flag and COUPLING keywords. These will be included in the output. You must choose the type of
coupling using the type keyword, you can also supply custom Karplus parameters using TYPE=CUSTOM and the A, B, C
and SHIFT keywords. You will need to make sure you are using the correct dihedral angle:

- Ha-N: \f$\psi\f$
- Ha-HN: \f$\phi\f$
- N-C\f$\gamma\f$: \f$\chi_1\f$
- CO-C\f$\gamma\f$: \f$\chi_1\f$

J-couplings can be used to calculate a Metainference score using the internal keyword DOSCORE and all the options
of \ref METAINFERENCE .

\par Examples

In the following example we calculate the Ha-N J-coupling from a set of atoms involved in
dihedral \f$\psi\f$ angles in the peptide backbone. We also add the experimental datapoints and compute
the correlation and other measures and finally print the results.

\plumedfile

MOLINFO MOLTYPE=protein STRUCTURE=peptide.pdb
WHOLEMOLECULES ENTITY0=1-111

JCOUPLING ...
    ADDCOUPLINGS
    TYPE=HAN
    ATOMS1=@psi-2 COUPLING1=-0.49
    ATOMS2=@psi-4 COUPLING2=-0.54
    ATOMS3=@psi-5 COUPLING3=-0.53
    ATOMS4=@psi-7 COUPLING4=-0.39
    ATOMS5=@psi-8 COUPLING5=-0.39
    LABEL=jhan
... JCOUPLING

jhanst: STATS ARG=(jhan\.j_.*) PARARG=(jhan\.exp_.*)

PRINT ARG=jhanst.*,jhan.* FILE=COLVAR STRIDE=100
\endplumedfile

*/
//+ENDPLUMEDOC

class JCoupling :
  public MetainferenceBase
{
private:
  bool pbc;
  enum { HAN, HAHN, CCG, NCG, CUSTOM };
  unsigned ncoupl_;
  double ka_;
  double kb_;
  double kc_;
  double kshift_;

public:
  static void registerKeywords(Keywords& keys);
  explicit JCoupling(const ActionOptions&);
  void calculate();
  void update();
};

PLUMED_REGISTER_ACTION(JCoupling, "JCOUPLING")

void JCoupling::registerKeywords(Keywords& keys) {
  componentsAreNotOptional(keys);
  useCustomisableComponents(keys);
  MetainferenceBase::registerKeywords(keys);
  keys.addFlag("NOPBC",false,"ignore the periodic boundary conditions when calculating distances");
  keys.add("numbered", "ATOMS", "the 4 atoms involved in each of the bonds for which you wish to calculate the J-coupling. "
           "Keywords like ATOMS1, ATOMS2, ATOMS3,... should be listed and one J-coupling will be "
           "calculated for each ATOMS keyword you specify.");
  keys.reset_style("ATOMS", "atoms");
  keys.addFlag("ADDCOUPLINGS", false, "Set this flag if you want to have fixed components with the experimental values.");
  keys.add("compulsory", "TYPE", "Type of J-coupling to compute (HAN,HAHN,CCG,NCG,CUSTOM)");
  keys.add("optional", "A", "Karplus parameter A");
  keys.add("optional", "B", "Karplus parameter B");
  keys.add("optional", "C", "Karplus parameter C");
  keys.add("optional", "SHIFT", "Angle shift in radians");
  keys.add("numbered", "COUPLING", "Add an experimental value for each coupling");
  keys.addOutputComponent("j", "default", "the calculated J-coupling");
  keys.addOutputComponent("exp", "ADDCOUPLINGS", "the experimental J-coupling");
}

JCoupling::JCoupling(const ActionOptions&ao):
  PLUMED_METAINF_INIT(ao),
  pbc(true)
{
  bool nopbc = !pbc;
  parseFlag("NOPBC", nopbc);
  pbc =! nopbc;

  // Read in the atoms
  vector<AtomNumber> t, atoms;
  for (int i = 1; ; ++i) {
    parseAtomList("ATOMS", i, t );
    if (t.empty()) {
      break;
    }

    if (t.size() != 4) {
      std::string ss;
      Tools::convert(i, ss);
      error("ATOMS" + ss + " keyword has the wrong number of atoms");
    }

    // This makes the distance calculation easier later on (see Torsion implementation)
    atoms.push_back(t[0]);
    atoms.push_back(t[1]);
    atoms.push_back(t[1]);
    atoms.push_back(t[2]);
    atoms.push_back(t[2]);
    atoms.push_back(t[3]);
    t.resize(0);
  }

  // We now have 6 atoms per datapoint
  ncoupl_ = atoms.size()/6;

  // Parse J-Coupling type, this will determine the Karplus parameters
  unsigned jtype_ = CUSTOM;
  string string_type;
  parse("TYPE", string_type);
  if(string_type == "HAN") {
    jtype_ = HAN;
  } else if(string_type == "HAHN") {
    jtype_ = HAHN;
  } else if(string_type == "CCG") {
    jtype_ = CCG;
  } else if(string_type == "NCG") {
    jtype_ = NCG;
  } else if(string_type == "CUSTOM") {
    jtype_ = CUSTOM;
  } else {
    error("Unknown J-coupling type!");
  }

  // Optionally add an experimental value (like with RDCs)
  vector<double> coupl;
  bool addcoupling = false;
  parseFlag("ADDCOUPLINGS", addcoupling);
  if (addcoupling||getDoScore()) {
    coupl.resize(ncoupl_);
    unsigned ntarget = 0;
    for (unsigned i = 0; i < ncoupl_; ++i) {
      if (!parseNumbered("COUPLING", i+1, coupl[i])) {
        break;
      }
      ntarget++;
    }
    if (ntarget != ncoupl_) {
      error("found wrong number of COUPLING values");
    }
  }

  // For custom types we allow use of custom Karplus parameters
  if (jtype_ == CUSTOM) {
    parse("A", ka_);
    parse("B", kb_);
    parse("C", kc_);
    parse("SHIFT", kshift_);
  }


  // Set Karplus parameters
  switch (jtype_) {
  case HAN:
    ka_ = -0.88;
    kb_ = -0.61;
    kc_ = -0.27;
    kshift_ = pi / 3.0;
    log.printf("J-coupling type is HAN, with A: %f, B: %f, C: %f, angle shift: %f\n", ka_, kb_, kc_, kshift_);
    log << "  Bibliography "
        << plumed.cite("Wang A C, Bax A, J. Am. Chem. Soc. 117, 1810 (1995)") << "\n";
    break;
  case HAHN:
    ka_ = 7.09;
    kb_ = -1.42;
    kc_ = 1.55;
    kshift_ = -pi / 3.0;
    log.printf("J-coupling type is HAHN, with A: %f, B: %f, C: %f, angle shift: %f\n", ka_, kb_, kc_, kshift_);
    log << "  Bibliography "
        << plumed.cite("Hu J-S, Bax A, J. Am. Chem. Soc. 119, 6360 (1997)") << "\n";
    break;
  case CCG:
    ka_ = 2.31;
    kb_ = -0.87;
    kc_ = 0.55;
    kshift_ = (2.0 * pi) / 3.0;
    log.printf("J-coupling type is CCG, with A: %f, B: %f, C: %f, angle shift: %f\n", ka_, kb_, kc_, kshift_);
    log << "  Bibliography "
        << plumed.cite("Perez C, Löhr F, Rüterjans H, Schmidt J, J. Am. Chem. Soc. 123, 7081 (2001)") << "\n";
    break;
  case NCG:
    ka_ = 1.29;
    kb_ = -0.49;
    kc_ = 0.37;
    kshift_ = 0.0;
    log.printf("J-coupling type is NCG, with A: %f, B: %f, C: %f, angle shift: %f\n", ka_, kb_, kc_, kshift_);
    log << "  Bibliography "
        << plumed.cite("Perez C, Löhr F, Rüterjans H, Schmidt J, J. Am. Chem. Soc. 123, 7081 (2001)") << "\n";
    break;
  case CUSTOM:
    log.printf("J-coupling type is custom, with A: %f, B: %f, C: %f, angle shift: %f\n", ka_, kb_, kc_, kshift_);
    break;
  }

  for (unsigned i = 0; i < ncoupl_; ++i) {
    log.printf("  The %uth J-Coupling is calculated from atoms : %d %d %d %d.",
               i+1, atoms[2*i].serial(), atoms[2*i+1].serial(), atoms[2*i+2].serial(), atoms[2*i+3].serial());
    if (addcoupling) {
      log.printf(" Experimental J-Coupling is %f.", coupl[i]);
    }
    log.printf("\n");
  }

  if (pbc) {
    log.printf("  using periodic boundary conditions\n");
  } else {
    log.printf("  without periodic boundary conditions\n");
  }

  if(!getDoScore()) {
    for (unsigned i = 0; i < ncoupl_; i++) {
      std::string num; Tools::convert(i, num);
      addComponentWithDerivatives("j_" + num);
      componentIsNotPeriodic("j_" + num);
    }
  } else {
    for (unsigned i = 0; i < ncoupl_; i++) {
      std::string num; Tools::convert(i, num);
      addComponent("j_" + num);
      componentIsNotPeriodic("j_" + num);
    }
  }

  if (addcoupling||getDoScore()) {
    for (unsigned i = 0; i < ncoupl_; i++) {
      std::string num; Tools::convert(i, num);
      addComponent("exp_" + num);
      componentIsNotPeriodic("exp_" + num);
      Value* comp = getPntrToComponent("exp_" + num);
      comp->set(coupl[i]);
    }
  }

  requestAtoms(atoms);
  if(getDoScore()) {
    setParameters(coupl);
    Initialise(ncoupl_);
  }
  setDerivatives();
  checkRead();
}

void JCoupling::calculate() {
  if (pbc) {
    makeWhole();
  }

  vector<Vector> deriv;
  if(getDoScore()) {
    deriv.resize(ncoupl_*6, Vector{0.,0.,0.});
  }

  const double omp_dummy = 0.0;
  const unsigned nt = OpenMP::getGoodNumThreads(&omp_dummy, ncoupl_ / 6);
  #pragma omp parallel num_threads(nt)
  {
    #pragma omp for
    // Loop through atoms, with steps of 6 atoms (one iteration per datapoint)
    for (unsigned r = 0; r < (ncoupl_ * 6); r += 6) {
      // Index is the datapoint index
      const unsigned index = r / 6;

      // 6 atoms -> 3 vectors
      Vector d0, d1, d2;
      d0 = delta(getPosition(r + 1), getPosition(r + 0));
      d1 = delta(getPosition(r + 3), getPosition(r + 2));
      d2 = delta(getPosition(r + 5), getPosition(r + 4));

      // Calculate dihedral with 3 vectors, get the derivatives
      Vector dd0, dd1, dd2;
      PLMD::Torsion t;
      const double torsion = t.compute(d0, d1, d2, dd0, dd1, dd2);

      // Calculate the Karplus relation and its derivative
      const double theta = torsion + kshift_;
      const double cos_theta = cos(theta);
      const double j = ka_ * cos_theta * cos_theta + kb_ * cos_theta + kc_;
      const double derj = sin(theta) * (-1.0 * (2.0 * ka_ * cos_theta + kb_));
      string num; Tools::convert(index,num);
      Value* val=getPntrToComponent("j_"+num);
      val->set(j);
      if(getDoScore()) {
        setCalcData(index, j);
        deriv[r+0] = derj * dd0;
        deriv[r+1] = derj * -dd0;
        deriv[r+2] = derj * dd1;
        deriv[r+3] = derj * -dd1;
        deriv[r+4] = derj * dd2;
        deriv[r+5] = derj * -dd2;
      } else {
        setAtomsDerivatives(val, r + 0, derj * dd0);
        setAtomsDerivatives(val, r + 1, derj * -dd0);
        setAtomsDerivatives(val, r + 2, derj * dd1);
        setAtomsDerivatives(val, r + 3, derj * -dd1);
        setAtomsDerivatives(val, r + 4, derj * dd2);
        setAtomsDerivatives(val, r + 5, derj * -dd2);

        Tensor virial;
        virial-=Tensor(getPosition(r+0),derj * dd0);
        virial-=Tensor(getPosition(r+1),derj * -dd0);
        virial-=Tensor(getPosition(r+2),derj * dd1);
        virial-=Tensor(getPosition(r+3),derj * -dd1);
        virial-=Tensor(getPosition(r+4),derj * dd2);
        virial-=Tensor(getPosition(r+5),derj * -dd2);
        setBoxDerivatives(val,virial);
      }
    }
  }

  if(getDoScore()) {
    /* Metainference */
    double score = getScore();
    setScore(score);

    /* calculate final derivatives */
    Tensor virial;
    Value* val=getPntrToComponent("score");
    for (unsigned r = 0; r < (ncoupl_ * 6); r += 6) {
      const unsigned index = r / 6;
      setAtomsDerivatives(val, r + 0, deriv[r+0]*getMetaDer(index));
      virial-=Tensor(getPosition(r+0), deriv[r+0]*getMetaDer(index));
      setAtomsDerivatives(val, r + 1, deriv[r+1]*getMetaDer(index));
      virial-=Tensor(getPosition(r+1), deriv[r+1]*getMetaDer(index));
      setAtomsDerivatives(val, r + 2, deriv[r+2]*getMetaDer(index));
      virial-=Tensor(getPosition(r+2), deriv[r+2]*getMetaDer(index));
      setAtomsDerivatives(val, r + 3, deriv[r+3]*getMetaDer(index));
      virial-=Tensor(getPosition(r+3), deriv[r+3]*getMetaDer(index));
      setAtomsDerivatives(val, r + 4, deriv[r+4]*getMetaDer(index));
      virial-=Tensor(getPosition(r+4), deriv[r+4]*getMetaDer(index));
      setAtomsDerivatives(val, r + 5, deriv[r+5]*getMetaDer(index));
      virial-=Tensor(getPosition(r+5), deriv[r+5]*getMetaDer(index));
    }
    setBoxDerivatives(val, virial);
  }
}

void JCoupling::update() {
  // write status file
  if(getWstride()>0&& (getStep()%getWstride()==0 || getCPT()) ) writeStatus();
}

}
}
