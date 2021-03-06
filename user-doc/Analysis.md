\page Analysis Analysis

PLUMED can be used to analyse trajectories either on the fly during an MD run or via
postprocessing a trajectory using \ref driver.  About the simplest form of analysis 
that PLUMED can perform involves printing information to a file.  PLUMED can output
various different kinds of information to files as described below:

@PRINTANALYSIS@  

The \ref UPDATE_IF action allows you to do more complex things using the above print
commands. As detailed in the documentation for \ref UPDATE_IF when you put any of the above 
actions within an UPDATE_IF block then data will only be output to the file if colvars
are within particular ranges.  In other words, the above printing commands, in tandem 
with \ref UPDATE_IF, allow you to identify the frames in your trajectory that satisfy
some particular criteria and output information on those frames only.

Another useful command is the \ref COMMITTOR command.  This command can only be used when running
an molecular dynamics trajectory - it cannot be used when analysing a trajectory using \ref driver.
As detailed in the documentation for \ref COMMITTOR this command tells PLUMED (and the underlying 
MD code) to stop the calculation one some criteria is satisified.

A number of more complicated forms of analysis can be performed that take a number of frames from 
the trajectory as input.  In all these commands the STRIDE keyword is used to tell PLUMED how 
frequently to collect data from the trajectory.  In all these methods the output from the analysis
is a form of enseble average.  If you are running with a bias it is thus likely that you may want 
to reweight the trajectory frames in order to remove the effect the bias has on the static behavoir
of the system.  The following methods can thus be used to calculate weights for the various trajectory
frames so that the final ensemble average is an average for the cannonical ensemble at the appropriate 
temperature.

@REWEIGHTING@

You can then calculate ensemble averages using the following actions.

@GRIDCALC@

For many of the above commands data is accumulated on the grids.  These grids can be further 
analysed using one of the actions detailed below at some time.  

@GRIDANALYSIS@

As an example the following set of commands instructs PLUMED to calculate the distance between 
atoms 1 and 2 for every 5th frame in the trajectory and to accumulate a histogram from this data
which will be output every 100 steps (i.e. when 20 distances have been added to the histogram).

\plumedfile
x: DISTANCE ATOMS=1,2
h: HISTOGRAM ARG=x GRID_MIN=0.0 GRID_MAX=3.0 GRID_BIN=100 BANDWIDTH=0.1 STRIDE=5
DUMPGRID GRID=h FILE=histo STRIDE=100 
\endplumedfile

It is important to note when using commands such as the above the first frame in the trajectory is assumed 
to be the initial configuration that was input to the MD code. It is thus ignored.  Furthermore, if you are 
running with driver and you would like to analyse the whole trajectory (without specifying its length) 
and then print the result you simply call \ref DUMPGRID (or any of the commands above) without a STRIDE 
keyword as shown in the example below. 

\plumedfile
x: DISTANCE ATOMS=1,2
h: HISTOGRAM ARG=x GRID_MIN=0.0 GRID_MAX=3.0 GRID_BIN=100 BANDWIDTH=0.1 STRIDE=5
DUMPGRID GRID=h FILE=histo 
\endplumedfile

Please note that even with this calculation the first frame in the trajectory is ignored when computing the 
histogram.

Notice that all the commands for calculating smooth functions described above calculate some sort of 
average.  There are two ways that you may wish to average the data in your trajectory:

- You might want to calculate block averages in which the first \f$N\f$N frames in your trajectory are
averaged separately to the second block of \f$N\f$ frames.  If this is the case you should use the 
keyword CLEAR in the input to the action that calculates the smooth function.  This keyword is used to 
specify how frequently you are clearing the stored data.

- You might want to calculate an accumulate an average over the whole trajectory and output the average
accumulated at step \f$N\f$, step \f$2N\f$...  This is what PLUMED does by default so you do not need to 
use CLEAR in this case.

\section dimred Dimensionality Reduction

The remainder of the analysis tools within PLUMED allow one to do some form of dimensionality 
reduction as detailed below.

@DIMRED@

As with the grids described previously the STRIDE keyword tells PLUMED how frequently to collect data from 
the trajectory.  The RUN keyword then tells PLUMED how frequently to do the dimensionality reduction.  As
described above if RUN is not present and you are analysing trajectories using \ref driver all the data in 
the traejctory (with the expection of the first frame) will be analysed. 

