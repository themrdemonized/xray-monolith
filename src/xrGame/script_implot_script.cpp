#include "pch_script.h"
#include "script_implot.h"
#include "implot/implot.h"
#include "implot/implot_internal.h"
#include "imgui/imgui.h"

using namespace luabind;
#pragma optimize("s",on)

// ImPlot dereferences the current plot with no null return, so a Lua call outside BeginPlot/EndPlot is refused here
static bool implot_in_plot()
{
	return ImPlot::GetCurrentContext() && ImPlot::GetCurrentPlot();
}

static bool implot_in_setup()
{
	return implot_in_plot() && !ImPlot::GetCurrentPlot()->SetupLocked;
}

// ImPlot indexes Axes[] with the Lua value unchecked, and a disabled axis trips a compiled-out assert
static bool implot_axis_ok(ImAxis axis)
{
	return axis >= 0 && axis < ImAxis_COUNT && ImPlot::GetCurrentPlot()->Axes[axis].Enabled;
}

// Style for the next series only, set by SetNextLineStyle/SetNextFillStyle and reset once a series or the plot consumes it
static ImPlotSpec s_implot_next;

static ImPlotSpec implot_take_spec(ImPlotItemFlags flags = 0)
{
	ImPlotSpec spec = s_implot_next;
	spec.Flags = flags;
	s_implot_next = ImPlotSpec();
	return spec;
}

// ImPlot series take parallel x[] and y[] double arrays. Lua hands them as 1-based number
// tables; copy once into reused scratch buffers, then call ImPlot with a defaulted ImPlotSpec.
// Returns the points read: 0 for a non-table, else up to the first missing or non-number element.
static xr_vector<double> s_implot_x;
static xr_vector<double> s_implot_y;

static int implot_read_xy(const luabind::object& xs, const luabind::object& ys, int count)
{
	if (!implot_in_plot() || count < 0 || !xs || xs.type() != LUA_TTABLE || !ys || ys.type() != LUA_TTABLE) count = 0;
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

// Third array for the band form of PlotShaded, read after implot_read_xy with the same stop rule
static xr_vector<double> s_implot_y2;

static int implot_read_y2(const luabind::object& ys2, int count)
{
	if (!ys2 || ys2.type() != LUA_TTABLE) count = 0;
	s_implot_y2.resize(count);
	for (int i = 0; i < count; ++i)
	{
		if (ys2[i + 1].type() != LUA_TNUMBER)
			return i;
		s_implot_y2[i] = object_cast<double>(ys2[i + 1]);
	}
	return count;
}

static bool ImPlot_BeginPlot(LPCSTR title, Fvector2 size, ImPlotFlags flags)
{
	if (!ImPlot::GetCurrentContext() || ImPlot::GetCurrentPlot()) return false;
	s_implot_next = ImPlotSpec();
	return ImPlot::BeginPlot(title, *(ImVec2*)&size, flags);
}

static void ImPlot_EndPlot()
{
	s_implot_next = ImPlotSpec();
	if (implot_in_plot()) ImPlot::EndPlot();
}

static void ImPlot_SetupAxes(LPCSTR x_label, LPCSTR y_label, ImPlotAxisFlags x_flags, ImPlotAxisFlags y_flags)
{
	if (implot_in_setup()) ImPlot::SetupAxes(x_label, y_label, x_flags, y_flags);
}

static void ImPlot_SetupAxisLimits(ImAxis axis, double v_min, double v_max, ImPlotCond cond)
{
	if (implot_in_setup() && implot_axis_ok(axis)) ImPlot::SetupAxisLimits(axis, v_min, v_max, cond);
}

// ImPlot copies the labels into the axis ticker, so the scratch strings may die after the call
static xr_vector<double> s_implot_ticks;
static xr_vector<xr_string> s_implot_tick_text;
static xr_vector<const char*> s_implot_tick_ptr;

static void ImPlot_SetupAxisTicks(ImAxis axis, const luabind::object& values, const luabind::object& labels, int count, bool keep_default)
{
	if (!implot_in_setup() || !implot_axis_ok(axis) || count <= 0 || !values || values.type() != LUA_TTABLE) return;
	const bool has_labels = labels && labels.type() == LUA_TTABLE;
	s_implot_ticks.clear();
	s_implot_tick_text.clear();
	for (int i = 0; i < count; ++i)
	{
		if (values[i + 1].type() != LUA_TNUMBER) break;
		if (has_labels && labels[i + 1].type() != LUA_TSTRING) break;
		s_implot_ticks.push_back(object_cast<double>(values[i + 1]));
		if (has_labels) s_implot_tick_text.push_back(object_cast<LPCSTR>(labels[i + 1]));
	}
	if (s_implot_ticks.empty()) return;
	s_implot_tick_ptr.clear();
	for (const xr_string& text : s_implot_tick_text) s_implot_tick_ptr.push_back(text.c_str());
	ImPlot::SetupAxisTicks(axis, s_implot_ticks.data(), (int)s_implot_ticks.size(), has_labels ? s_implot_tick_ptr.data() : nullptr, keep_default);
}

static void ImPlot_SetupLegend(ImPlotLocation location, ImPlotLegendFlags flags)
{
	if (implot_in_setup()) ImPlot::SetupLegend(location, flags);
}

static void ImPlot_SetNextLineStyle(Fcolor& col, float weight)
{
	s_implot_next.LineColor = *(ImVec4*)&col;
	if (weight > 0.f) s_implot_next.LineWeight = weight;
}

static void ImPlot_SetNextFillStyle(Fcolor& col, float alpha)
{
	s_implot_next.FillColor = *(ImVec4*)&col;
	s_implot_next.FillAlpha = alpha < 0.f ? 0.f : (alpha > 1.f ? 1.f : alpha);
}

static bool ImPlot_IsPlotHovered()
{
	return implot_in_plot() && ImPlot::IsPlotHovered();
}

// Mouse position in plot units on the X1/Y1 axes; (0,0) outside a plot
static Fvector2 ImPlot_GetPlotMousePos()
{
	Fvector2 pos{ 0.f, 0.f };
	if (!implot_in_plot()) return pos;
	const ImPlotPoint point = ImPlot::GetPlotMousePos();
	pos.set((float)point.x, (float)point.y);
	return pos;
}

static void ImPlot_PlotLine(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count)
{
	count = implot_read_xy(xs, ys, count);
	const ImPlotSpec spec = implot_take_spec();
	if (count > 0)
		ImPlot::PlotLine(label, s_implot_x.data(), s_implot_y.data(), count, spec);
}

static void ImPlot_PlotShaded(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count, double yref)
{
	count = implot_read_xy(xs, ys, count);
	const ImPlotSpec spec = implot_take_spec();
	if (count > 0)
		ImPlot::PlotShaded(label, s_implot_x.data(), s_implot_y.data(), count, yref, spec);
}

static void ImPlot_PlotShadedBand(LPCSTR label, const luabind::object& xs, const luabind::object& ys1, const luabind::object& ys2, int count)
{
	count = implot_read_xy(xs, ys1, count);
	count = implot_read_y2(ys2, count);
	const ImPlotSpec spec = implot_take_spec();
	if (count > 0)
		ImPlot::PlotShaded(label, s_implot_x.data(), s_implot_y.data(), s_implot_y2.data(), count, spec);
}

static void ImPlot_PlotScatter(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count)
{
	count = implot_read_xy(xs, ys, count);
	const ImPlotSpec spec = implot_take_spec();
	if (count > 0)
		ImPlot::PlotScatter(label, s_implot_x.data(), s_implot_y.data(), count, spec);
}

static void ImPlot_PlotBars(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count, double bar_size)
{
	count = implot_read_xy(xs, ys, count);
	const ImPlotSpec spec = implot_take_spec();
	if (count > 0)
		ImPlot::PlotBars(label, s_implot_x.data(), s_implot_y.data(), count, bar_size, spec);
}

static void ImPlot_PlotBarsFlags(LPCSTR label, const luabind::object& xs, const luabind::object& ys, int count, double bar_size, ImPlotBarsFlags flags)
{
	count = implot_read_xy(xs, ys, count);
	const ImPlotSpec spec = implot_take_spec(flags);
	if (count > 0)
		ImPlot::PlotBars(label, s_implot_x.data(), s_implot_y.data(), count, bar_size, spec);
}

void ScriptImPlot::script_register(::lua_State* L)
{
	module(L, "ImPlot")
	[
		def("BeginPlot", &ImPlot_BeginPlot),
		def("EndPlot", &ImPlot_EndPlot),
		def("SetupAxes", &ImPlot_SetupAxes),
		def("SetupAxisLimits", &ImPlot_SetupAxisLimits),
		def("SetupAxisTicks", &ImPlot_SetupAxisTicks),
		def("SetupLegend", &ImPlot_SetupLegend),
		def("SetNextLineStyle", &ImPlot_SetNextLineStyle),
		def("SetNextFillStyle", &ImPlot_SetNextFillStyle),
		def("IsPlotHovered", &ImPlot_IsPlotHovered),
		def("GetPlotMousePos", &ImPlot_GetPlotMousePos),
		def("PlotLine", &ImPlot_PlotLine),
		def("PlotShaded", &ImPlot_PlotShaded),
		def("PlotShadedBand", &ImPlot_PlotShadedBand),
		def("PlotScatter", &ImPlot_PlotScatter),
		def("PlotBars", &ImPlot_PlotBars),
		def("PlotBars", &ImPlot_PlotBarsFlags)
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
		],

		class_<enum_exporter<ImPlotBarsFlags_>>("ImPlotBarsFlags")
		.enum_("ImPlotBarsFlags")
		[
			value("None", (int)ImPlotBarsFlags_None),
			value("Horizontal", (int)ImPlotBarsFlags_Horizontal)
		],

		class_<enum_exporter<ImPlotLegendFlags_>>("ImPlotLegendFlags")
		.enum_("ImPlotLegendFlags")
		[
			value("None", (int)ImPlotLegendFlags_None),
			value("NoButtons", (int)ImPlotLegendFlags_NoButtons),
			value("NoMenus", (int)ImPlotLegendFlags_NoMenus),
			value("Outside", (int)ImPlotLegendFlags_Outside),
			value("Horizontal", (int)ImPlotLegendFlags_Horizontal)
		],

		class_<enum_exporter<ImPlotLocation_>>("ImPlotLocation")
		.enum_("ImPlotLocation")
		[
			value("Center", (int)ImPlotLocation_Center),
			value("North", (int)ImPlotLocation_North),
			value("South", (int)ImPlotLocation_South),
			value("West", (int)ImPlotLocation_West),
			value("East", (int)ImPlotLocation_East),
			value("NorthWest", (int)ImPlotLocation_NorthWest),
			value("NorthEast", (int)ImPlotLocation_NorthEast),
			value("SouthWest", (int)ImPlotLocation_SouthWest),
			value("SouthEast", (int)ImPlotLocation_SouthEast)
		]
	];
}
