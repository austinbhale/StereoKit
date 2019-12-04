#include "stereokit_ui.h"
#include "_stereokit_ui.h"
#include "math.h"

#include <DirectXMath.h>
using namespace DirectX;
#include <vector>
using namespace std;

///////////////////////////////////////////

namespace sk {

struct layer_t {
	vec3   offset_initial;
	vec3   offset;
	vec2   size;
	float  line_height;
	float  max_x;
};
struct ui_hand_t {
	vec3            finger;
	vec3            finger_prev;
	vec3            finger_world;
	vec3            finger_world_prev;
	bool            tracked;
	uint64_t        focused_prev = {};
	uint64_t        focused = {};
	uint64_t        active = {};
};

vector<layer_t> skui_layers;
mesh_t          skui_box;
mesh_t          skui_cylinder;
material_t      skui_mat;
material_t      skui_mat_dbg;
font_t          skui_font;
text_style_t    skui_font_style;
material_t      skui_font_mat;
ui_hand_t       skui_hand[2];

ui_settings_t skui_settings = {
	10 * mm2m, // padding
	10 * mm2m, // gutter
	15 * mm2m, // depth
	.4f,       // backplate_depth
	0.5f * mm2m, // backplate_border
};
float skui_fontsize = 15*mm2m;

vec3  skui_prev_offset;
float skui_prev_line_height;

uint64_t skui_anim_id;
float    skui_anim_time;

const color128 skui_color_border = { 1,1,1,1 };
color128 skui_palette[5];

///////////////////////////////////////////

uint64_t ui_hash(const char *string);

// Layout
void ui_push_pose  (pose_t pose, vec3 offset);
void ui_pop_pose   ();
void ui_layout_box (vec2 content_size, vec3 &out_position, vec2 &out_final_size, bool32_t use_content_padding = true);
void ui_reserve_box(vec2 size);

void ui_nextline    ();
void ui_sameline    ();
void ui_reserve_box (vec2 size);
void ui_space       (float space);

// Interaction
bool32_t      ui_in_box            (vec3 pt1, vec3 pt2, bounds_t box);
int32_t       ui_box_interaction_1h(uint64_t id, vec3 box_unfocused_start, vec3 box_unfocused_size, vec3 box_focused_start, vec3 box_focused_size, button_state_ *out_focus_state);
button_state_ ui_button_behavior   (vec3 window_relative_pos, vec2 size, uint64_t id, float &finger_offset);

// Base render types
void ui_box      (vec3 start, vec3 size, material_t material, color128 color);
void ui_text     (vec3 start, const char *text, text_align_ position = text_align_x_left | text_align_y_top);

///////////////////////////////////////////

inline bounds_t ui_size_box(vec3 top_left, vec3 dimensions) {
	return { top_left - dimensions/2, dimensions };
}

///////////////////////////////////////////

inline void ui_anim_start(uint64_t id) {
	if (skui_anim_id != id) {
		skui_anim_id = id;
		skui_anim_time = time_getf();
	}
}

///////////////////////////////////////////

inline bool ui_anim_has(uint64_t id, float duration) {
	if (id == skui_anim_id) {
		if ((time_getf() - skui_anim_time) < duration)
			return true;
		skui_anim_id = 0;
	}
	return false;
}

///////////////////////////////////////////

inline float ui_anim_elapsed(uint64_t id, float duration = 1, float max = 1) {
	return skui_anim_id == id ? fminf(max, (time_getf() - skui_anim_time) / duration) : 0;
}

///////////////////////////////////////////

void ui_settings(ui_settings_t settings) {
	if (settings.backplate_border == 0) settings.backplate_border = 0.5f * mm2m;
	if (settings.backplate_depth  == 0) settings.backplate_depth  = 0.4f;
	if (settings.depth            == 0) settings.depth   = 15 * mm2m;
	if (settings.gutter           == 0) settings.gutter  = 20 * mm2m;
	if (settings.padding          == 0) settings.padding = 10 * mm2m;
	skui_settings = settings;
}

///////////////////////////////////////////

void ui_set_color(color128 color) {
	vec3 hsv = color_to_hsv(color);
	
	skui_palette[0] = color;
	skui_palette[1] = color_hsv(fmodf(hsv.x + 0.5f, 1), hsv.y,          hsv.z * 0.7f, color.a);
	skui_palette[2] = color_hsv(hsv.x,                  hsv.y * 0.075f, hsv.z * 0.7f, color.a);
	skui_palette[3] = color_hsv(fmodf(hsv.x + 0.5f, 1), hsv.y * 0.075f, hsv.z * 0.7f, color.a);
	skui_palette[4] = color128{1, 1, 1, 1};
}

///////////////////////////////////////////

bool ui_init() {
	ui_set_color(color_hsv(0.07f, 0.8f, 0.5f, 1));

	skui_box      = mesh_gen_cube(vec3_one);
	skui_cylinder = mesh_gen_cylinder(1, 1, {0,0,1}, 24);
	skui_mat      = material_find("default/material_ui");
	skui_mat_dbg  = material_copy(skui_mat);
	material_set_transparency(skui_mat_dbg, transparency_blend);
	material_set_color(skui_mat_dbg, "color", { 0,1,0,0.25f });

	skui_font_mat   = material_find("default/material_font");
	material_set_queue_offset(skui_font_mat, -12);
	skui_font       = font_find("default/font");
	skui_font_style = text_make_style(skui_font, skui_fontsize, skui_font_mat, color_to_32( skui_palette[4] ));
	
	skui_layers.push_back({});

	return true;
}

///////////////////////////////////////////

void ui_update() {
	if (skui_layers.size() > 1 || skui_layers.size() == 0)
		log_err("ui: Mismatching number of Begin/End calls!");

	skui_layers[0] = {};

	for (size_t i = 0; i < handed_max; i++) {
		const hand_t &hand = input_hand((handed_)i);

		skui_hand[i].finger_world_prev = skui_hand[i].finger_world;
		skui_hand[i].finger_world      = hand.fingers[1][4].position;
		skui_hand[i].focused_prev = skui_hand[i].focused;

		skui_hand[i].focused = 0;
		skui_hand[i].finger       = matrix_mul_point(hierarchy_to_local(), skui_hand[i].finger_world);
		skui_hand[i].finger_prev  = matrix_mul_point(hierarchy_to_local(), skui_hand[i].finger_world_prev);
		skui_hand[i].tracked     = hand.state & input_state_tracked;
	}
}

///////////////////////////////////////////

void ui_shutdown() {
	mesh_release(skui_box);
	mesh_release(skui_cylinder);
	material_release(skui_mat);
	material_release(skui_font_mat);
	font_release(skui_font);
}

///////////////////////////////////////////

// djb2 hash: http://www.cse.yorku.ca/~oz/hash.html
uint64_t ui_hash(const char *string) {
	unsigned long hash = 5381;
	int c;
	while (c = *string++)
		hash = ((hash << 5) + hash) + c; // hash * 33 + c
	return hash;
}

///////////////////////////////////////////
//////////////   Layout!   ////////////////
///////////////////////////////////////////

void ui_push_pose(pose_t pose, vec3 offset) {
	vec3   right = pose.orientation * vec3_right;
	matrix trs   = matrix_trs(pose.position + right*offset, pose.orientation);
	hierarchy_push(trs);

	skui_layers.push_back(layer_t{
		vec3{skui_settings.padding, -skui_settings.padding}, 
		vec3{skui_settings.padding, -skui_settings.padding}, {0,0}, 0, 0
		});

	for (size_t i = 0; i < handed_max; i++) {
		const hand_t &hand = input_hand((handed_)i);
		skui_hand[i].finger      = matrix_mul_point(hierarchy_to_local(), skui_hand[i].finger_world);
		skui_hand[i].finger_prev = matrix_mul_point(hierarchy_to_local(), skui_hand[i].finger_world_prev);
	}
}

///////////////////////////////////////////

void ui_pop_pose() {
	hierarchy_pop();
	skui_layers.pop_back();
}

///////////////////////////////////////////

void ui_layout_area(vec3 start, vec2 dimensions) {
	layer_t &layer = skui_layers.back();
	layer.offset_initial = start;
	layer.offset         = layer.offset_initial - vec3{ skui_settings.padding, -skui_settings.padding };
	layer.size           = dimensions;
	layer.max_x          = 0;
	layer.line_height    = 0;
}

///////////////////////////////////////////

void ui_layout_box(vec2 content_size, vec3 &out_position, vec2 &out_final_size, bool32_t use_content_padding) {
	out_position   = skui_layers.back().offset;
	out_final_size = content_size;
	if (use_content_padding)
		out_final_size += vec2{ skui_settings.padding, skui_settings.padding }*2;

	// If this is not the first element, and it goes outside the active window
	if (out_position.x            != -skui_settings.padding &&
		skui_layers.back().size.x != 0                      &&
		out_position.x - out_final_size.x < skui_layers.back().offset_initial.x - skui_layers.back().size.x + skui_settings.padding)
	{
		ui_nextline();
		out_position = skui_layers.back().offset;
	}
}

///////////////////////////////////////////

void ui_reserve_box(vec2 size) {
	if (skui_layers.back().max_x > skui_layers.back().offset.x - size.x - skui_settings.padding)
		skui_layers.back().max_x = skui_layers.back().offset.x - size.x - skui_settings.padding;
	if (skui_layers.back().line_height < size.y)
		skui_layers.back().line_height = size.y;

	skui_layers.back().offset -= vec3{ size.x + skui_settings.gutter, 0, 0 };
}

///////////////////////////////////////////

void ui_nextline() {
	layer_t &layer = skui_layers.back();
	skui_prev_offset      = layer.offset;
	skui_prev_line_height = layer.line_height;

	layer.offset.x    = skui_layers.back().offset_initial.x - skui_settings.padding;
	layer.offset.y   -= skui_layers.back().line_height + skui_settings.gutter;
	layer.line_height = 0;
}

///////////////////////////////////////////

void ui_sameline() {
	layer_t &layer = skui_layers.back();
	layer.offset      = skui_prev_offset;
	layer.line_height = skui_prev_line_height;
}

///////////////////////////////////////////

float ui_line_height() {
	return skui_settings.padding * 2 + skui_fontsize;
}

///////////////////////////////////////////

void ui_space(float space) {
	if (skui_layers.back().offset.x == skui_settings.padding)
		skui_layers.back().offset.y -= space;
	else
		skui_layers.back().offset.x += space;
}

///////////////////////////////////////////
///////////   Interaction!   //////////////
///////////////////////////////////////////

int32_t ui_box_interaction_1h(uint64_t id, vec3 box_unfocused_start, vec3 box_unfocused_size, vec3 box_focused_start, vec3 box_focused_size, button_state_ *out_focus_state) {
	int32_t hand = -1;

	for (int32_t i = 0; i < handed_max; i++) {
		button_state_ focus_state = button_state_up;
		bool focused   = skui_hand[i].focused_prev == id;
		vec3 box_start = box_unfocused_start;
		vec3 box_size  = box_unfocused_size;
		if (focused) {
			focus_state = button_state_down;
			box_start = box_focused_start;
			box_size  = box_focused_size;
		}

		if (skui_hand[i].tracked && ui_in_box(skui_hand[i].finger, skui_hand[i].finger_prev, ui_size_box(box_start, box_size))) {
			hand = i;
			skui_hand[i].focused = id;
			focus_state = button_state_down;
			if (!focused)
				focus_state |= button_state_just_down;
		} else if (focused) {
			focus_state = button_state_up | button_state_just_up;
		}

		if (hand == i && out_focus_state != nullptr)
			*out_focus_state = focus_state;
	}
	return hand;
}

///////////////////////////////////////////

bool32_t ui_in_box(vec3 pt, vec3 pt_prev, bounds_t box) {
	//render_add_mesh(skui_box, skui_mat_dbg, matrix_trs(box.center, quat_identity, box.dimensions));
	return bounds_line_contains(box, pt, pt_prev);
}

///////////////////////////////////////////

button_state_ ui_button_behavior(vec3 window_relative_pos, vec2 size, uint64_t id, float &finger_offset) {
	button_state_ result = button_state_up;

	// Button interaction focus is detected in the front half of the button to prevent 'reverse'
	// or 'side' presses where the finger comes from the back or side.
	//
	// Once focused is gained, interaction is tracked within a volume that extends from the 
	// front, to a good distance through the button's back. This is to help when the user's
	// finger inevitably goes completely through the button. May consider expanding the volume 
	// a bit too on the X/Y axes later.
	vec3    box_start = window_relative_pos + vec3{ 0, 0, -skui_settings.depth/2.f };
	vec3    box_size  = vec3{ size.x, size.y, skui_settings.depth/2.f };
	button_state_ focus;
	int32_t hand = ui_box_interaction_1h(id,
		box_start, box_size,
		box_start + vec3{ 0,0, skui_settings.depth * 4 },
		box_size  + vec3{ 0,0, skui_settings.depth * 4 },
		&focus);

	// If a hand is interacting, adjust the button surface accordingly
	finger_offset = skui_settings.depth;
	if (hand != -1) {
		finger_offset = -skui_hand[hand].finger.z - window_relative_pos.z;
		if (finger_offset < skui_settings.depth / 2) {
			result = button_state_down;
			if (skui_hand[hand].active != id) {
				skui_hand[hand].active = id;
				result |= button_state_just_down;
			}
		} else if (skui_hand[hand].active == id) {
			skui_hand[hand].active = 0;
			result |= button_state_just_up;
		}
		finger_offset = fmaxf(skui_settings.backplate_depth*skui_settings.depth + mm2m, finger_offset);
	} else if (focus & button_state_just_up && skui_hand[hand].active == id) {
		skui_hand[hand].active = 0;
		result |= button_state_just_up;
	}

	return result;
}

///////////////////////////////////////////
////////   Base Visual Elements   /////////
///////////////////////////////////////////

void ui_box(vec3 start, vec3 size, material_t material, color128 color) {
	vec3   pos = start - size / 2;
	matrix mx  = matrix_trs(pos, quat_identity, size);

	render_add_mesh(skui_box, material, mx, color);
}

///////////////////////////////////////////

void ui_cylinder(vec3 start, float radius, float depth, material_t material, color128 color) {
	vec3   pos = start - (vec3{ radius, radius, depth } / 2);
	matrix mx  = matrix_trs(pos, quat_identity, {radius, radius, depth});

	render_add_mesh(skui_cylinder, material, mx, color);
}

///////////////////////////////////////////

void ui_model_at(model_t model, vec3 start, vec3 size, color128 color) {
	matrix mx = matrix_trs(start, quat_identity, size);
	render_add_model(model, mx, color);
}

///////////////////////////////////////////

void ui_text(vec3 start, const char *text, text_align_ position) {
	text_add_at(text, matrix_identity, skui_font_style, position, text_align_x_left | text_align_y_top, start.x, start.y, start.z);
}

///////////////////////////////////////////
///////////   UI Components   /////////////
///////////////////////////////////////////

void ui_label(const char *text, bool32_t use_padding) {
	vec3  offset = skui_layers.back().offset;
	vec2  size   = text_size(text, skui_font_style);
	float pad    = use_padding ? skui_settings.padding : 0;

	ui_layout_box (size, offset, size, use_padding);
	ui_reserve_box(size);
	ui_text(offset - vec3{pad, pad, 2*mm2m }, text);
	ui_nextline();
}

///////////////////////////////////////////

void ui_image(sprite_t image, vec2 size) {
	float aspect     = sprite_get_aspect(image);
	vec3  offset     = skui_layers.back().offset;
	vec2  final_size = vec2{
		size.x==0 ? size.y/aspect : size.x, 
		size.y==0 ? size.x*aspect : size.y };

	ui_layout_box (final_size, offset, final_size);
	ui_reserve_box(final_size);
	ui_nextline();
	
	sprite_draw(image, matrix_trs(offset - vec3{ final_size.x, 0, 2*mm2m }, quat_identity, vec3{ final_size.x, final_size.y, 1 }));
}

///////////////////////////////////////////

bool32_t ui_button_at(vec3 window_relative_pos, vec2 size, const char *text) {
	uint64_t      id = ui_hash(text);
	float         finger_offset;
	button_state_ state = ui_button_behavior(window_relative_pos, size, id, finger_offset);

	if (state & button_state_just_down)
		ui_anim_start(id);
	float color_blend = state & button_state_down ? 1.5 : 1;
	float back_size   = skui_settings.backplate_border;
	if (ui_anim_has(id, .1f)) {
		float t     = ui_anim_elapsed    (id, .1f);
		back_size   = math_ease_hop      (back_size, fmaxf(mm2m*2, back_size*2.f), t);
		color_blend = math_ease_overshoot(1, 1.5f, 10, t);
	}

	ui_box (window_relative_pos,  vec3{ size.x,   size.y,   finger_offset }, skui_mat, skui_palette[2] * color_blend);
	ui_box (window_relative_pos + vec3{back_size, back_size, mm2m}, vec3{ size.x+back_size*2, size.y+back_size*2, skui_settings.backplate_depth*skui_settings.depth+mm2m }, skui_mat, skui_color_border * color_blend);
	ui_text(window_relative_pos - vec3{ size.x/2, size.y/2, finger_offset + 2*mm2m }, text, text_align_center);

	return state & button_state_just_down;
}

///////////////////////////////////////////

bool32_t ui_button(const char *text) {
	vec3 offset;
	vec2 size;
	ui_layout_box (text_size(text, skui_font_style), offset, size);
	ui_reserve_box(size);
	ui_nextline   ();

	return ui_button_at(offset, size, text);
}

///////////////////////////////////////////

bool32_t ui_toggle_at(vec3 window_relative_pos, vec2 size, const char *text, bool32_t &pressed) {
	uint64_t      id = ui_hash(text);
	float         finger_offset;
	button_state_ state = ui_button_behavior(window_relative_pos, size, id, finger_offset);

	if (state & button_state_just_down)
		ui_anim_start(id);
	float color_blend = pressed ? 1.5 : 1;
	float back_size   = skui_settings.backplate_border;
	if (ui_anim_has(id, .1f)) {
		float t     = ui_anim_elapsed    (id, .1f);
		back_size   = math_ease_hop      (back_size, fmaxf(mm2m*2, back_size*2.f), t);
		color_blend = math_ease_overshoot(1, 1.5f, 10, t);
	}

	if (state & button_state_just_down) {
		pressed = !pressed;
	}
	finger_offset = pressed ? fminf(skui_settings.backplate_depth*skui_settings.depth + mm2m, finger_offset) : finger_offset;

	ui_box (window_relative_pos,  vec3{ size.x,    size.y,   finger_offset }, skui_mat, skui_palette[2] * color_blend);
	ui_box (window_relative_pos + vec3{ back_size, back_size, mm2m}, vec3{ size.x+back_size*2, size.y+back_size*2, skui_settings.backplate_depth*skui_settings.depth+mm2m }, skui_mat, skui_color_border * color_blend);
	ui_text(window_relative_pos - vec3{ size.x/2,  size.y/2, finger_offset + 2*mm2m }, text, text_align_center);

	return state & button_state_just_down;
}

///////////////////////////////////////////

bool32_t ui_toggle(const char *text, bool32_t &pressed) {
	vec3 offset;
	vec2 size;
	ui_layout_box (text_size(text, skui_font_style), offset, size);
	ui_reserve_box(size);
	ui_nextline   ();

	return ui_toggle_at(offset, size, text, pressed);
}

///////////////////////////////////////////

bool32_t ui_button_round_at(vec3 window_relative_pos, float diameter, const char *text) {
	uint64_t      id = ui_hash(text);
	float         finger_offset;
	button_state_ state = ui_button_behavior(window_relative_pos, {diameter,diameter}, id, finger_offset);

	if (state & button_state_just_down)
		ui_anim_start(id);
	float color_blend = state & button_state_down ? 1.5 : 1;
	float back_size   = skui_settings.backplate_border;
	if (ui_anim_has(id, .1f)) {
		float t     = ui_anim_elapsed    (id, .1f);
		back_size   = math_ease_hop      (back_size, fmaxf(mm2m*2, back_size*2.f), t);
		color_blend = math_ease_overshoot(1, 1.5f, 10, t);
	}

	ui_cylinder(window_relative_pos, diameter, finger_offset, skui_mat, skui_palette[2] * color_blend);
	ui_cylinder(window_relative_pos + vec3{back_size, back_size, mm2m}, diameter+back_size*2, skui_settings.backplate_depth*skui_settings.depth+mm2m, skui_mat, skui_color_border * color_blend);
	ui_text    (window_relative_pos - vec3{ diameter/2, diameter/2, finger_offset + 2*mm2m }, text, text_align_center);

	return state & button_state_just_down;
}

///////////////////////////////////////////

bool32_t ui_button_round(const char *text, float diameter) {
	vec3 offset;
	vec2 size = diameter == 0 ? text_size(text, skui_font_style) : vec2{diameter, diameter};
	size = vec2_one * fmaxf(size.x, size.y);
	ui_layout_box (size, offset, size, false);
	ui_reserve_box(size);
	ui_nextline   ();

	return ui_button_round_at(offset, size.x, text);
}

///////////////////////////////////////////

void ui_model(model_t model, vec2 ui_size, float model_scale) {
	vec3 offset = skui_layers.back().offset;
	vec2 size   = ui_size + vec2{ skui_settings.padding, skui_settings.padding }*2;

	ui_reserve_box(size);
	size = size / 2;
	ui_model_at(model, { offset.x + size.x, offset.y - size.y, offset.z }, vec3_one * model_scale, { 1,1,1,1 });
	ui_nextline();
}

///////////////////////////////////////////

bool32_t ui_input(const char *id, char *buffer, int32_t buffer_size) {
	uint64_t id_hash= ui_hash(id);
	bool     result = false;
	bool     focused = false;
	vec3     offset = skui_layers.back().offset;
	vec2     size   = text_size(buffer, skui_font_style);
	size += vec2{ skui_settings.padding, skui_settings.padding } * 2;
	vec3 box_size = vec3{ size.x, size.y, skui_settings.depth/2 };

	for (size_t i = 0; i < handed_max; i++) {
		if (ui_in_box(skui_hand[i].finger, skui_hand[i].finger_prev, ui_size_box(offset, box_size))) {
			skui_hand[i].focused = id_hash;
			focused = true;
		}
	}
	if (focused) {
		char add = '\0';
		bool shift = input_key(key_shift) & button_state_down;
		if (input_key(key_backspace) & button_state_just_down) {
			size_t len = strlen(buffer);
			if (len >= 0) {
				buffer[len - 1] = '\0';
				result = true;
			}
		}
		if (input_key(key_space) & button_state_just_down) add = ' ';
		for (int32_t k = 0; k < 26; k++) {
			if (input_key((key_)(key_a + k)) & button_state_just_down) {
				add = (char)((shift ? 'A' : 'a') + k);
			}
		}
		for (int32_t k = 0; k < 10; k++) {
			if (input_key((key_)(key_0 + k)) & button_state_just_down) {
				const char *nums = ")!@#$%^&*(";
				add = (char)(shift ? nums[k] : '0'+k);
			}
		}
		if (add != '\0') {
			size_t len = strlen(buffer);
			if (len + 2 < buffer_size) {
				buffer[len] = add;
				buffer[len + 1] = '\0';
				result = true;
			}
		}
	}

	ui_reserve_box(size);
	ui_box (offset, vec3{ size.x, size.y, skui_settings.depth/2 }, skui_mat, skui_palette[2] * (focused ? 0.5f : 1.f) );
	ui_text(offset + vec3{ size.x/2, -size.y/2, skui_settings.depth/2 + 2*mm2m }, buffer, text_align_center);
	ui_nextline();

	return result;
}

///////////////////////////////////////////

bool32_t ui_hslider(const char *name, float &value, float min, float max, float step, float width) {
	uint64_t   id     = ui_hash(name);
	bool       result = false;
	float      color  = 1;
	vec3       offset = skui_layers.back().offset;

	// Find sizes of slider elements
	if (width == 0)
		width = skui_layers.back().size.x == 0 ? 0.1f : (skui_layers.back().size.x - skui_settings.padding) - skui_layers.back().offset.x;
	vec2 size = { width, skui_fontsize };
	float rule_size = size.y / 6.f;

	// Interaction code
	vec3     box_start = offset + vec3{ 0, 0, skui_settings.depth };
	vec3     box_size  = vec3{ size.x, size.y, skui_settings.depth*2 };
	bounds_t box       = ui_size_box(box_start, box_size);
	for (size_t i = 0; i < handed_max; i++) {
		if (ui_in_box(skui_hand[i].finger, skui_hand[i].finger_prev, box)) {
			skui_hand[i].focused = id;
			color = 1.5f;
			float new_val = min + (fabsf(skui_hand[i].finger.x - offset.x) / size.x) * (max - min);
			if (step != 0) {
				new_val = ((int)(((new_val - min) / step)+0.5f)) * step;
			}
			result = value != new_val;
			value  = new_val;

			if (result)
				skui_hand[i].active = id;
		}
	}

	// Draw the UI
	ui_reserve_box(size);
	float back_size = skui_settings.backplate_border;
	// Slide line
	ui_box(vec3{ offset.x, offset.y - size.y / 2.f + rule_size / 2.f, offset.z }, vec3{ size.x, rule_size, rule_size }, skui_mat, skui_palette[2] * color);
	ui_box(vec3{ offset.x+back_size, offset.y - size.y / 2.f + rule_size / 2.f + back_size, offset.z+mm2m }, vec3{ size.x+back_size*2, rule_size+back_size*2, rule_size*skui_settings.backplate_depth+mm2m }, skui_mat, skui_color_border * color);
	// Slide handle
	ui_box(vec3{ offset.x - ((value-min)/(max-min))*size.x - rule_size/2.f, offset.y, offset.z}, vec3{rule_size, size.y, skui_settings.depth}, skui_mat, skui_palette[2] * color);
	ui_nextline();
	
	return result;
}

///////////////////////////////////////////

bool32_t ui_affordance_begin(const char *text, pose_t &movement, vec3 center, vec3 dimensions, bool32_t draw) {
	uint64_t id = ui_hash(text);
	bool result = false;
	float color = 1;

	matrix to_local = hierarchy_to_local();
	ui_push_pose(movement, vec3{ 0,0,0 });

	vec3     box_start = center;//   +vec3{ skui_settings.padding, skui_settings.padding, skui_settings.padding };
	vec3     box_size  = dimensions + vec3{ skui_settings.padding, skui_settings.padding, skui_settings.padding } *2;
	bounds_t box       = bounds_t{center, box_size};
	
	static vec3 start_aff_pos[2] = {};
	static quat start_aff_rot[2] = { quat_identity,quat_identity };
	static vec3 start_tip_pos[2] = {};
	static quat start_tip_rot[2] = { quat_identity,quat_identity };
	for (size_t i = 0; i < handed_max; i++) {
		// Skip this if something else has some focus!
		if (!skui_hand[i].tracked || (skui_hand[i].focused_prev != 0 && skui_hand[i].focused_prev != id))
			continue;

		if (ui_in_box(skui_hand[i].finger, skui_hand[i].finger_prev, box)) {
			skui_hand[i].focused = id;
			color = 0.75f;
		}

		if (skui_hand[i].focused == id || skui_hand[i].active == id) {
			
			const hand_t &hand = input_hand((handed_)i);
			if (hand.state & input_state_justpinch) {
				skui_hand[i].active = id;
				start_aff_pos[i] = movement.position;
				start_aff_rot[i] = movement.orientation;
				start_tip_pos[i] = matrix_mul_point   ( to_local, input_hand((handed_)i).root.position );
				start_tip_rot[i] = matrix_mul_rotation( to_local, input_hand((handed_)i).root.orientation);
			}
			if (skui_hand[i].active == id) {
				color = 0.5f;
				result = true;

				vec3 curr_pos = matrix_mul_point   (to_local, input_hand((handed_)i).root.position);
				quat curr_rot = matrix_mul_rotation(to_local, input_hand((handed_)i).root.orientation);

				quat rot = quat_difference(start_tip_rot[i], curr_rot);

				movement.position    = curr_pos + rot*(start_aff_pos[i] - start_tip_pos[i]);
				movement.orientation = start_aff_rot[i]*rot;
				if (hand.state & input_state_unpinch) {
					skui_hand[i].active = 0;
				}
				ui_pop_pose();
				ui_push_pose(movement, vec3{ 0,0,0 });
			}
		}
	}

	if (draw) {
		ui_box(center+dimensions/2, dimensions, skui_mat, skui_palette[0] * color);
		ui_nextline();
	}
	return color < 1;
}

///////////////////////////////////////////

void ui_affordance_end() {
	ui_pop_pose();
}

///////////////////////////////////////////

void ui_window_begin(const char *text, pose_t &pose, vec2 window_size, bool32_t show_header) {
	if (window_size.x == 0) window_size.x = 32*cm2m;

	if (show_header) {
		vec2 size      = text_size(text, skui_font_style);
		vec3 box_start = vec3{ 0, 0, 0 };
		vec3 box_size  = vec3{ window_size.x, size.y+skui_settings.padding*2, skui_settings.depth };
		ui_affordance_begin(text, pose, box_start, box_size, true);
		ui_layout_area({ window_size.x / 2,0,0 }, window_size);
		skui_layers.back().offset.y = -(box_size.y/2 + skui_settings.padding);

		ui_text(box_start + vec3{window_size.x/2-skui_settings.padding,box_size.y/2 - skui_settings.padding, -skui_settings.depth/2 - 2*mm2m}, text);
		
		ui_nextline();
	} else {
		ui_push_pose(pose, { window_size.x / 2,0,0 });
		ui_layout_area(vec3_zero, window_size);
	}
}

///////////////////////////////////////////

void ui_window_end() {
	ui_pop_pose();
}

} // namespace sk