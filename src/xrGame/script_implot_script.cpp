#include "pch_script.h"
#include "script_implot.h"
#include "implot/implot.h"
#include "imgui/imgui.h"

using namespace luabind;
#pragma optimize("s",on)

// ImPlot series take parallel x[] and y[] double arrays. Lua hands them as 1-based number
// tables; copy once into reused scratch buffers, then call ImPlot with a defaulted ImPlotSpec.
// Returns the points read: 0 for a non-table, else up to the first missing or non-number element.
static xr_vector<double> s_implot_x;
static xr_vector<double> s_implot_y;

static int implot_read_xy(const luabind::object& xs, const luabind::object& ys, int count)
{
	if (count < 0 || !xs || xs.type() != LUA_TTABLE || !ys || ys.type() != LUA_TTABLE) count = 0;
	s_implot_x.resize(count);
	s_implot_y.resize(count);
	for (int i = 0; i < count; ++i)
	{
		// type check first: this luabind converts nil to 0 for double (policy.hpp PRIMITIVE_MATCHER(double))
		if (xs[i + 1].type() != LUA_TNUMBER || ys[i + 1].type() != LUA_TNUMBER)
			return i;
		s_implot_x[i] = object_cast<double>(xs[i + 1]);
		s_implot_y[i] = object_cast<double>(ys[i + 1]);
	}
	return count;
}

static bool ImPlot_BeginPlot(LPCSTR title, Fvector2 size, ImPlotFlags flags)
{
	return ImPlot::BeginPlot(title, *(ImVec2*)&size, flags);
}

static void ImPlot_PlotLine(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count)
{
	count = implot_read_xy(xs, ys, count);
	if (count > 0)
		ImPlot::PlotLine(label, s_implot_x.data(), s_implot_y.data(), count);
}

static void ImPlot_PlotShaded(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count, double yref)
{
	count = implot_read_xy(xs, ys, count);
	if (count > 0)
		ImPlot::PlotShaded(label, s_implot_x.data(), s_implot_y.data(), count, yref);
}

static void ImPlot_PlotScatter(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count)
{
	count = implot_read_xy(xs, ys, count);
	if (count > 0)
		ImPlot::PlotScatter(label, s_implot_x.data(), s_implot_y.data(), count);
}

static void ImPlot_PlotBars(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count, double bar_size)
{
	count = implot_read_xy(xs, ys, count);
	if (count > 0)
		ImPlot::PlotBars(label, s_implot_x.data(), s_implot_y.data(), count, bar_size);
}

void ScriptImPlot::script_register(::lua_State* L)
{
	module(L, "ImPlot")
	[
		def("BeginPlot", &ImPlot_BeginPlot),
		def("EndPlot", &ImPlot::EndPlot),
		def("SetupAxes", &ImPlot::SetupAxes),
		def("SetupAxisLimits", &ImPlot::SetupAxisLimits),
		def("PlotLine", &ImPlot_PlotLine),
		def("PlotShaded", &ImPlot_PlotShaded),
		def("PlotScatter", &ImPlot_PlotScatter),
		def("PlotBars", &ImPlot_PlotBars)
	],

	module(L)
	[
		class_<enum_exporter<ImPlotFlags_>>("ImPlotFlags")
		.enum_("ImPlotFlags")
		[
			value("None", (int)ImPlotFlags_None),
			value("NoTitle", (int)ImPlotFlags_NoTitle),
			value("NoLegend", (int)ImPlotFlags_NoLegend),
			value("NoMouseText", (int)ImPlotFlags_NoMouseText),
			value("NoInputs", (int)ImPlotFlags_NoInputs),
			value("NoMenus", (int)ImPlotFlags_NoMenus),
			value("NoBoxSelect", (int)ImPlotFlags_NoBoxSelect),
			value("Crosshairs", (int)ImPlotFlags_Crosshairs),
			value("CanvasOnly", (int)ImPlotFlags_CanvasOnly)
		],

		class_<enum_exporter<ImPlotAxisFlags_>>("ImPlotAxisFlags")
		.enum_("ImPlotAxisFlags")
		[
			value("None", (int)ImPlotAxisFlags_None),
			value("NoLabel", (int)ImPlotAxisFlags_NoLabel),
			value("NoGridLines", (int)ImPlotAxisFlags_NoGridLines),
			value("NoTickMarks", (int)ImPlotAxisFlags_NoTickMarks),
			value("NoTickLabels", (int)ImPlotAxisFlags_NoTickLabels),
			value("Opposite", (int)ImPlotAxisFlags_Opposite),
			value("AutoFit", (int)ImPlotAxisFlags_AutoFit),
			value("NoDecorations", (int)ImPlotAxisFlags_NoDecorations)
		],

		class_<enum_exporter<ImPlotCond_>>("ImPlotCond")
		.enum_("ImPlotCond")
		[
			value("None", (int)ImPlotCond_None),
			value("Always", (int)ImPlotCond_Always),
			value("Once", (int)ImPlotCond_Once)
		],

		class_<enum_exporter<ImAxis_>>("ImAxis")
		.enum_("ImAxis")
		[
			value("X1", (int)ImAxis_X1),
			value("X2", (int)ImAxis_X2),
			value("X3", (int)ImAxis_X3),
			value("Y1", (int)ImAxis_Y1),
			value("Y2", (int)ImAxis_Y2),
			value("Y3", (int)ImAxis_Y3)
		]
	];
}
