# SIMET-LMAPD

This file describes behavior that is implemented in the simet fork of
lmapd, which has not been merged in upstream lmapd yet.

## JSON support

Simet-lmapd has JSON as its primary serialization format for structured
data, instead of XML, due to code-size concerns.  Features might be
implemented initially only in JSON mode, and XML mode will be added should
we get enough spare time or a strong indication that upstream is going to
merge the resulting effort.

## Extensions to IETF lmapd

### Task (action) output dataflow

1. Output of an action goes to an "incoming" folder that belongs to the
   destination schedule.  This is done in a way that it is "atomic" for
   the purposes of the next step.

2. When lmapd will start executing actions from a schedule, it moves all
   output from that schedule's "incoming" folder into the schedule's
   "active" folder.

3. Actions on a schedule can process data from that schedule's "active"
   folder, and must ignore the contents of the "incoming" folder.

4. Any new data delivered to a schedule that is already running will go
   into that schedule's "incoming" folder, and will be processed only the
   next time it is executed (i.e. no new data is delivered to the actions
   of a schedule that is already being executed).

5. As an exception, an action in a *sequential* schedule that has that
   same schedule as its output destination will deliver directly to the
   "active" folder, thus its output will be immediately available to the
   next action in that schedule's sequence.

The "incoming" folder is a subdir of the schedule's workspace.  The
"active" folder is the schedule's workspace.

### Structured task (action) output

This is only relevant when lmapctl is used to generate reports, otherwise
task output format is opaque to lmapd/simet-lmapd.

The internal LMAP report generator of IETF lmapd accepts only single-table
CSV output from tasks (running as actions inside a schedule).  This means
a task could only output one result table in LMAP report terms.

Simet-lmapd has extended "lmapctl" to accept structured output from task
actions, in JSON format (XML is pending implementation).  This allows a
task action to output as many LMAP report tables as it needs (including
none).

***Caveats***: the current implementation does no format autodetection,
thus all task output being processed by a single "lmapctl report" must be
of the same type.  One must use the new "-i <format>" option for "lmapctl"
to select the task output serialization format (json or xml).  If the "-i"
option is not used, either the default CSV parser or autodetection (should
it ever get implemented) will be used, instead.

JSON format for task action output:

"lmapctl -i json" will accept either a list of tables, or a single instance of
a report result table.  To make things easier for task writers, it will accept:

	{ "table:" [ <object>, ... ], }
	[ <object>, ... ]
	<object>
	(empty file)

where <object> is an ietf-lmap-report::report.result.table member.
