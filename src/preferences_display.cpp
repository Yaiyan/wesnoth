/* $Id$ */
/*
   Copyright (C) 2003-5 by David White <davidnwhite@verizon.net>
   Part of the Battle for Wesnoth Project http://www.wesnoth.org/

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/
#include "global.hpp"

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "cursor.hpp"
#include "display.hpp"
#include "gettext.hpp"
#include "hotkeys.hpp"
#include "marked-up_text.hpp"
#include "preferences_display.hpp"
#include "show_dialog.hpp"
#include "video.hpp"
#include "wml_separators.hpp"
#include "widgets/button.hpp"
#include "widgets/label.hpp"
#include "widgets/menu.hpp"
#include "widgets/slider.hpp"
#include "widgets/textbox.hpp"
#include "theme.hpp"

#include <vector>
#include <string>

namespace preferences {

display* disp = NULL;

display_manager::display_manager(display* d)
{
	disp = d;

	load_hotkeys();

	set_grid(grid());
	set_turbo(turbo());
	set_fullscreen(fullscreen());
	set_gamma(gamma());
	set_colour_cursors(preferences::get("colour_cursors") == "yes");
}

display_manager::~display_manager()
{
	disp = NULL;
}

void set_fullscreen(bool ison)
{
	_set_fullscreen(ison);

	if(disp != NULL) {
		const std::pair<int,int>& res = resolution();
		CVideo& video = disp->video();
		if(video.isFullScreen() != ison) {
			const int flags = ison ? FULL_SCREEN : 0;
			const int bpp = video.modePossible(res.first,res.second,16,flags);
			if(bpp > 0) {
				video.setMode(res.first,res.second,bpp,flags);
				disp->redraw_everything();
			} else if(video.modePossible(1024,768,16,flags)) {
				set_resolution(std::pair<int,int>(1024,768));
			} else {
				gui::show_dialog(*disp,NULL,"",_("The video mode could not be changed. Your window manager must be set to 16 bits per pixel to run the game in windowed mode. Your display must support 1024x768x16 to run the game full screen."),
				                 gui::MESSAGE);
			}
		}
	}
}

void set_resolution(const std::pair<int,int>& resolution)
{
	std::pair<int,int> res = resolution;

	// - Ayin: disabled the following code. Why would one want to enforce that?
	// Some 16:9, or laptop screens, may have resolutions which do not
	// comply to this rule (see bug 10630). I'm commenting this until it
	// proves absolutely necessary.
	//
	//make sure resolutions are always divisible by 4
	//res.first &= ~3;
	//res.second &= ~3;

	bool write_resolution = true;

	if(disp != NULL) {
		CVideo& video = disp->video();
		const int flags = fullscreen() ? FULL_SCREEN : 0;
		const int bpp = video.modePossible(res.first,res.second,16,flags);
		if(bpp != 0) {
			video.setMode(res.first,res.second,bpp,flags);
			disp->redraw_everything();

		} else {
			write_resolution = false;
			gui::show_dialog(*disp,NULL,"",_("The video mode could not be changed. Your window manager must be set to 16 bits per pixel to run the game in windowed mode. Your display must support 1024x768x16 to run the game full screen."),gui::MESSAGE);
		}
	}

	if(write_resolution) {
		const std::string postfix = fullscreen() ? "resolution" : "windowsize";
		preferences::set('x' + postfix, lexical_cast<std::string>(res.first));
		preferences::set('y' + postfix, lexical_cast<std::string>(res.second));
	}
}

void set_turbo(bool ison)
{
	_set_turbo(ison);

	if(disp != NULL) {
		disp->set_turbo(ison);
	}
}

void set_adjust_gamma(bool val)
{
	//if we are turning gamma adjustment off, then set it to '1.0'
	if(val == false && adjust_gamma()) {
		CVideo& video = disp->video();
		video.setGamma(1.0);
	}

	_set_adjust_gamma(val);
}

void set_gamma(int gamma)
{
	_set_gamma(gamma);

	if(adjust_gamma()) {
		CVideo& video = disp->video();
		video.setGamma((float)gamma / 100);
	}
}

void set_grid(bool ison)
{
	_set_grid(ison);

	if(disp != NULL) {
		disp->set_grid(ison);
	}
}

void set_colour_cursors(bool value)
{
	_set_colour_cursors(value);

	cursor::use_colour(value);
}

namespace {

class preferences_dialog : public gui::preview_pane
{
public:
	preferences_dialog(display& disp, const config& game_cfg);
	void join();

private:

	void process_event();
	bool left_side() const { return false; }
	void set_selection(int index);
	void update_location(SDL_Rect const &rect);
	const config* get_advanced_pref() const;
	void set_advanced_menu();

	gui::slider music_slider_, sound_slider_, scroll_slider_, gamma_slider_, buffer_size_slider_;
	gui::button turbo_button_, show_ai_moves_button_,
	            show_grid_button_, show_floating_labels_button_, turn_dialog_button_,
	            turn_bell_button_, show_team_colours_button_, show_colour_cursors_button_,
	            show_haloing_button_, gamma_button_,
	            flip_time_button_, advanced_button_, sound_button_, music_button_, advanced_sound_button_, normal_sound_button_,
	            sample_rate_button1_, sample_rate_button2_, sample_rate_button3_, confirm_sound_button_;
	gui::label music_label_, sound_label_, scroll_label_, gamma_label_,  sample_rate_label_, buffer_size_label_;
	gui::textbox sample_rate_input_;

	unsigned slider_label_width_;

	gui::menu advanced_;
	int advanced_selection_;

	enum TAB { GENERAL_TAB, DISPLAY_TAB, SOUND_TAB, ADVANCED_TAB,
	           /*extra tab*/
	           ADVANCED_SOUND_TAB};
	TAB tab_;
	display &disp_;
	const config& game_cfg_;
};

preferences_dialog::preferences_dialog(display& disp, const config& game_cfg)
	: gui::preview_pane(disp.video()),
	  music_slider_(disp.video()), sound_slider_(disp.video()),
	  scroll_slider_(disp.video()), gamma_slider_(disp.video()),
	  buffer_size_slider_(disp.video()),
	  turbo_button_(disp.video(), _("Accelerated Speed"), gui::button::TYPE_CHECK),
	  show_ai_moves_button_(disp.video(), _("Skip AI Moves"), gui::button::TYPE_CHECK),
	  show_grid_button_(disp.video(), _("Show Grid"), gui::button::TYPE_CHECK),
	  show_floating_labels_button_(disp.video(), _("Show Floating Labels"), gui::button::TYPE_CHECK),
	  turn_dialog_button_(disp.video(), _("Turn Dialog"), gui::button::TYPE_CHECK),
	  turn_bell_button_(disp.video(), _("Turn Bell"), gui::button::TYPE_CHECK),
	  show_team_colours_button_(disp.video(), _("Show Team Colors"), gui::button::TYPE_CHECK),
	  show_colour_cursors_button_(disp.video(), _("Show Color Cursors"), gui::button::TYPE_CHECK),
	  show_haloing_button_(disp.video(), _("Show Haloing Effects"), gui::button::TYPE_CHECK),
	  gamma_button_(disp.video(), _("Adjust Gamma"), gui::button::TYPE_CHECK),
	  flip_time_button_(disp.video(), _("Reverse Time Graphics"), gui::button::TYPE_CHECK),
	  advanced_button_(disp.video(), "", gui::button::TYPE_CHECK),
	  sound_button_(disp.video(), _("Sound effects"), gui::button::TYPE_CHECK),
	  music_button_(disp.video(), _("Music"), gui::button::TYPE_CHECK),
	  advanced_sound_button_(disp.video(), _("Advanced Mode")),
	  normal_sound_button_(disp.video(), _("Normal Mode")),
	  sample_rate_button1_(disp.video(), "22050", gui::button::TYPE_CHECK),
	  sample_rate_button2_(disp.video(), "44100", gui::button::TYPE_CHECK),
	  sample_rate_button3_(disp.video(), _("Custom"), gui::button::TYPE_CHECK),
	  confirm_sound_button_(disp.video(), _("Apply")),
	  music_label_(disp.video(), _("Music Volume:")), sound_label_(disp.video(), _("SFX Volume:")),
	  scroll_label_(disp.video(), _("Scroll Speed:")), gamma_label_(disp.video(), _("Gamma:")),
	  sample_rate_label_(disp.video(), _("Sample Rate (Hz):")), buffer_size_label_(disp.video(),""),
	  sample_rate_input_(disp.video(), 70),
	  slider_label_width_(0), advanced_(disp.video(),
	  std::vector<std::string>(),false,-1,-1,NULL,&gui::menu::bluebg_style), advanced_selection_(-1),
	  tab_(GENERAL_TAB), disp_(disp), game_cfg_(game_cfg)
{
	// FIXME: this box should be vertically centered on the screen, but is not
#ifdef USE_TINY_GUI
	set_measurements(180, 180);		  // FIXME: should compute this, but using what data ?
#else
	set_measurements(400, 400);
#endif


	sound_button_.set_check(sound_on());
	sound_button_.set_help_string(_("Sound effects on/off"));
	sound_slider_.set_min(0);
	sound_slider_.set_max(128);
	sound_slider_.set_value(sound_volume());
	sound_slider_.set_help_string(_("Change the sound effects volume"));

	music_button_.set_check(music_on());
	music_button_.set_help_string(_("Music on/off"));
	music_slider_.set_min(0);
	music_slider_.set_max(128);
	music_slider_.set_value(music_volume());
	music_slider_.set_help_string(_("Change the music volume"));

	sample_rate_label_.set_help_string(_("Change the sample rate"));
	std::string rate = lexical_cast<std::string>(sample_rate());
	if (rate == "22050")
		sample_rate_button1_.set_check(true);
	else if (rate == "44100")
		sample_rate_button2_.set_check(true);
	else
		sample_rate_button3_.set_check(true);
	sample_rate_input_.set_text(rate);
	sample_rate_input_.set_help_string(_("User defined sample rate"));
	confirm_sound_button_.enable(sample_rate_button3_.checked());

	buffer_size_slider_.set_min(0);
	buffer_size_slider_.set_max(3);
	int v = sound_buffer_size()/512 - 1;
	buffer_size_slider_.set_value(v);
	//avoid sound reset the first time we load advanced sound
	buffer_size_slider_.value_change();
	buffer_size_slider_.set_help_string(_("Change the buffer size"));
	std::stringstream buf;
	buf << _("Buffer Size: ") << sound_buffer_size();
	buffer_size_label_.set_text(buf.str());
	buffer_size_label_.set_help_string(_("Change the buffer size"));


	scroll_slider_.set_min(1);
	scroll_slider_.set_max(100);
	scroll_slider_.set_value(scroll_speed());
	scroll_slider_.set_help_string(_("Change the speed of scrolling around the map"));

	gamma_button_.set_check(adjust_gamma());
	gamma_button_.set_help_string(_("Change the brightness of the display"));

	gamma_slider_.set_min(50);
	gamma_slider_.set_max(200);
	gamma_slider_.set_value(gamma());
	gamma_slider_.set_help_string(_("Change the brightness of the display"));

	turbo_button_.set_check(turbo());
	turbo_button_.set_help_string(_("Make units move and fight faster"));

	show_ai_moves_button_.set_check(!show_ai_moves());
	show_ai_moves_button_.set_help_string(_("Do not animate AI units moving"));

	show_grid_button_.set_check(grid());
	show_grid_button_.set_help_string(_("Overlay a grid onto the map"));

	show_floating_labels_button_.set_check(show_floating_labels());
	show_floating_labels_button_.set_help_string(_("Show text above a unit when it is hit to display damage inflicted"));

	turn_dialog_button_.set_check(turn_dialog());
	turn_dialog_button_.set_help_string(_("Display a dialog at the beginning of your turn"));

	turn_bell_button_.set_check(turn_bell());
	turn_bell_button_.set_help_string(_("Play a bell sound at the beginning of your turn"));

	show_team_colours_button_.set_check(show_side_colours());
	show_team_colours_button_.set_help_string(_("Show a colored circle around the base of each unit to show which side it is on"));

	flip_time_button_.set_check(flip_time());
	flip_time_button_.set_help_string(_("Choose whether the sun moves left-to-right or right-to-left"));

	show_colour_cursors_button_.set_check(use_colour_cursors());
	show_colour_cursors_button_.set_help_string(_("Use colored mouse cursors (may be slower)"));

	show_haloing_button_.set_check(show_haloes());
	show_haloing_button_.set_help_string(_("Use graphical special effects (may be slower)"));

	set_advanced_menu();
}

void preferences_dialog::join()
{
	//join the current event_context
	widget::join();

	//instruct all member widgets to join the current event_context
	music_slider_.join();
	sound_slider_.join();
	scroll_slider_.join();
	gamma_slider_.join();
	buffer_size_slider_.join();
	turbo_button_.join();
	show_ai_moves_button_.join();
	show_grid_button_.join();
	show_floating_labels_button_.join();
	turn_dialog_button_.join();
	turn_bell_button_.join();
	show_team_colours_button_.join();
	show_colour_cursors_button_.join();
	show_haloing_button_.join();
	gamma_button_.join();
	flip_time_button_.join();
	advanced_button_.join();
	sound_button_.join();
	music_button_.join();
	advanced_sound_button_.join();
	normal_sound_button_.join();
	sample_rate_button1_.join();
	sample_rate_button2_.join();
	sample_rate_button3_.join();
	confirm_sound_button_.join();
	music_label_.join();
	sound_label_.join();
	scroll_label_.join();
	gamma_label_.join();
	sample_rate_label_.join();
	buffer_size_label_.join();
	sample_rate_input_.join();
	advanced_.join();
}

void preferences_dialog::update_location(SDL_Rect const &rect)
{
	bg_register(rect);

	const int top_border = 28;
	const int right_border = font::relative_size(10);
#if USE_TINY_GUI
	const int item_interline = 20;
#else
	const int item_interline = 50;
#endif

	// General tab
	int ypos = rect.y + top_border;
	scroll_label_.set_location(rect.x, ypos);
	SDL_Rect scroll_rect = { rect.x + scroll_label_.width(), ypos,
	                         rect.w - scroll_label_.width() - right_border, 0 };
	scroll_slider_.set_location(scroll_rect);
	ypos += item_interline; turbo_button_.set_location(rect.x, ypos);
	ypos += item_interline; show_ai_moves_button_.set_location(rect.x, ypos);
	ypos += item_interline; turn_dialog_button_.set_location(rect.x, ypos);
	ypos += item_interline; turn_bell_button_.set_location(rect.x, ypos);
	ypos += item_interline; show_team_colours_button_.set_location(rect.x, ypos);
	ypos += item_interline; show_grid_button_.set_location(rect.x, ypos);

	// Display tab
	ypos = rect.y + top_border;
	gamma_button_.set_location(rect.x, ypos);
	ypos += item_interline;
	gamma_label_.set_location(rect.x, ypos);
	SDL_Rect gamma_rect = { rect.x + gamma_label_.width(), ypos,
	                        rect.w - gamma_label_.width() - right_border, 0 };
	gamma_slider_.set_location(gamma_rect);
	ypos += item_interline; flip_time_button_.set_location(rect.x,ypos);
	ypos += item_interline; show_floating_labels_button_.set_location(rect.x, ypos);
	ypos += item_interline; show_colour_cursors_button_.set_location(rect.x, ypos);
	ypos += item_interline; show_haloing_button_.set_location(rect.x, ypos);

	// Sound tab
	slider_label_width_ = maximum<unsigned>(music_label_.width(), sound_label_.width());
	ypos = rect.y + top_border;
	sound_button_.set_location(rect.x, ypos);

	ypos += item_interline;
	sound_label_.set_location(rect.x, ypos);
	const SDL_Rect sound_rect = { rect.x + slider_label_width_, ypos,
	                        rect.w - slider_label_width_ - right_border, 0 };
	sound_slider_.set_location(sound_rect);

	ypos += item_interline;
	music_button_.set_location(rect.x, ypos);

	ypos += item_interline;
	music_label_.set_location(rect.x, ypos);
	const SDL_Rect music_rect = { rect.x + slider_label_width_, ypos,
	                        rect.w - slider_label_width_ - right_border, 0 };
	music_slider_.set_location(music_rect);
	ypos += item_interline;
	const int asb_x = rect.x + rect.w - advanced_sound_button_.width() - 20;
	advanced_sound_button_.set_location(asb_x, ypos);

	//Advanced Sound tab
	ypos = rect.y + top_border;
	sample_rate_label_.set_location(rect.x, ypos);
	ypos += 20;
	int h_offset = rect.x + 20;
	sample_rate_button1_.set_location(h_offset, ypos);
	ypos += 20;
	sample_rate_button2_.set_location(h_offset, ypos);
	ypos += 20;
	sample_rate_button3_.set_location(h_offset, ypos);
	h_offset += sample_rate_button3_.width() + 5;
	sample_rate_input_.set_location(h_offset, ypos);
	h_offset += sample_rate_input_.width() + 5;
	confirm_sound_button_.set_location(h_offset, ypos);

	ypos += item_interline;
	buffer_size_label_.set_location(rect.x, ypos);
	ypos += 20;
	SDL_Rect buffer_rect = {rect.x + 20, ypos,
							rect.w - 20 - right_border, 0 };
	buffer_size_slider_.set_location(buffer_rect);
	ypos += item_interline;
	const int nsb_x = rect.x + rect.w - normal_sound_button_.width() - 20;
	normal_sound_button_.set_location(nsb_x, ypos);

	//Advanced tab
	ypos = rect.y + top_border;
	advanced_.set_location(rect.x,ypos);
	advanced_.set_max_height(height()-100);

	ypos += advanced_.height() + font::relative_size(14);

	advanced_button_.set_location(rect.x,ypos);

	set_selection(tab_);
}

void preferences_dialog::process_event()
{
	if (turbo_button_.pressed())
		set_turbo(turbo_button_.checked());
	if (show_ai_moves_button_.pressed())
		set_show_ai_moves(!show_ai_moves_button_.checked());
	if (show_grid_button_.pressed())
		set_grid(show_grid_button_.checked());
	if (show_floating_labels_button_.pressed())
		set_show_floating_labels(show_floating_labels_button_.checked());
	if (turn_bell_button_.pressed())
		set_turn_bell(turn_bell_button_.checked());
	if (turn_dialog_button_.pressed())
		set_turn_dialog(turn_dialog_button_.checked());
	if (show_team_colours_button_.pressed())
		set_show_side_colours(show_team_colours_button_.checked());
	if (show_colour_cursors_button_.pressed())
		set_colour_cursors(show_colour_cursors_button_.checked());
	if (show_haloing_button_.pressed())
		set_show_haloes(show_haloing_button_.checked());
	if (gamma_button_.pressed()) {
		set_adjust_gamma(gamma_button_.checked());
		bool enable_gamma = adjust_gamma();
		gamma_slider_.enable(enable_gamma);
		gamma_label_.enable(enable_gamma);
	}

	if (sound_button_.pressed()) {
		if(!set_sound(sound_button_.checked()))
			sound_button_.set_check(false);
	}
	set_sound_volume(sound_slider_.value());

	if (music_button_.pressed()) {
		if(!set_music(music_button_.checked()))
			music_button_.set_check(false);
	}
	set_music_volume(music_slider_.value());

	if (advanced_sound_button_.pressed())
		set_selection(ADVANCED_SOUND_TAB);

	//ADVANCED_SOUND_TAB
	bool apply = false;
	std::string rate;

	if (sample_rate_button1_.pressed()) {
		if (sample_rate_button1_.checked()) {
			sample_rate_button2_.set_check(false);
			sample_rate_button3_.set_check(false);
			confirm_sound_button_.enable(false);
			apply = true;
			rate = "22050";
		} else
			sample_rate_button1_.set_check(true);
	}
	if (sample_rate_button2_.pressed()) {
		if (sample_rate_button2_.checked()) {
			sample_rate_button1_.set_check(false);
			sample_rate_button3_.set_check(false);
			confirm_sound_button_.enable(false);
			apply = true;
			rate = "44100";
		} else
			sample_rate_button2_.set_check(true);
	}
	if (sample_rate_button3_.pressed()) {
		if (sample_rate_button3_.checked()) {
			sample_rate_button1_.set_check(false);
			sample_rate_button2_.set_check(false);
			confirm_sound_button_.enable(true);
		} else
			sample_rate_button3_.set_check(true);
	}
	if (confirm_sound_button_.pressed()) {
		apply = true;
		rate = sample_rate_input_.text();
	}

	if (apply)
		try {
		save_sample_rate(lexical_cast<unsigned int>(rate));
		} catch (bad_lexical_cast&) {
		}

	if (buffer_size_slider_.value_change()) {
		const size_t buffer_size = 512 << buffer_size_slider_.value();
		save_sound_buffer_size(buffer_size);
		std::stringstream buf;
		buf << _("Buffer Size: ") << buffer_size;
		buffer_size_label_.set_text(buf.str());
	}

	if (normal_sound_button_.pressed())
		set_selection(SOUND_TAB);
	//-----

	if (flip_time_button_.pressed())
		set_flip_time(flip_time_button_.checked());

	set_scroll_speed(scroll_slider_.value());
	set_gamma(gamma_slider_.value());

	if(advanced_.selection() != advanced_selection_) {
		advanced_selection_ = advanced_.selection();
		const config* const adv = get_advanced_pref();
		if(adv != NULL) {
			const config& pref = *adv;
			advanced_button_.set_width(0);
			advanced_button_.set_label(pref["name"]);
			std::string value = preferences::get(pref["field"]);
			if(value.empty()) {
				value = pref["default"];
			}

			advanced_button_.set_check(value == "yes");
		}
	}

	if(advanced_button_.pressed()) {
		const config* const adv = get_advanced_pref();
		if(adv != NULL) {
			const config& pref = *adv;
			preferences::set(pref["field"],
					 advanced_button_.checked() ? "yes" : "no");
			set_advanced_menu();
		}
	}
}

const config* preferences_dialog::get_advanced_pref() const
{
	const config::child_list& adv = game_cfg_.get_children("advanced_preference");
	if(advanced_selection_ >= 0 && advanced_selection_ < int(adv.size())) {
		return adv[advanced_selection_];
	} else {
		return NULL;
	}
}

void preferences_dialog::set_advanced_menu()
{
	std::vector<std::string> advanced_items;
	const config::child_list& adv = game_cfg_.get_children("advanced_preference");
	for(config::child_list::const_iterator i = adv.begin(); i != adv.end(); ++i) {
		std::ostringstream str;
		std::string field = preferences::get((**i)["field"]);
		if(field.empty()) {
			field = (**i)["default"];
		}

		if(field == "yes") {
			field = _("yes");
		} else if(field == "no") {
			field = _("no");
		}

		str << (**i)["name"] << COLUMN_SEPARATOR << field;
		advanced_items.push_back(str.str());
	}

	advanced_.set_items(advanced_items,true,true);
}

void preferences_dialog::set_selection(int index)
{
	tab_ = TAB(index);
	set_dirty();
	bg_restore();

	const bool hide_general = tab_ != GENERAL_TAB;
	scroll_label_.hide(hide_general);
	scroll_slider_.hide(hide_general);
	turbo_button_.hide(hide_general);
	show_ai_moves_button_.hide(hide_general);
	turn_dialog_button_.hide(hide_general);
	turn_bell_button_.hide(hide_general);
	show_team_colours_button_.hide(hide_general);
	show_grid_button_.hide(hide_general);

	const bool hide_display = tab_ != DISPLAY_TAB;
	gamma_label_.hide(hide_display);
	gamma_slider_.hide(hide_display);
	gamma_label_.enable(adjust_gamma());
	gamma_slider_.enable(adjust_gamma());
	gamma_button_.hide(hide_display);
	show_floating_labels_button_.hide(hide_display);
	show_colour_cursors_button_.hide(hide_display);
	show_haloing_button_.hide(hide_display);
	flip_time_button_.hide(hide_display);

	const bool hide_sound = tab_ != SOUND_TAB;
	music_button_.hide(hide_sound);
	music_label_.hide(hide_sound);
	music_slider_.hide(hide_sound);
	sound_button_.hide(hide_sound);
	sound_label_.hide(hide_sound);
	sound_slider_.hide(hide_sound);
	advanced_sound_button_.hide(hide_sound);

	const bool hide_advanced_sound = tab_ != ADVANCED_SOUND_TAB;
	sample_rate_label_.hide(hide_advanced_sound);
	sample_rate_button1_.hide(hide_advanced_sound);
	sample_rate_button2_.hide(hide_advanced_sound);
	sample_rate_button3_.hide(hide_advanced_sound);
	sample_rate_input_.hide(hide_advanced_sound);
	confirm_sound_button_.hide(hide_advanced_sound);
	buffer_size_label_.hide(hide_advanced_sound);
	buffer_size_slider_.hide(hide_advanced_sound);
	normal_sound_button_.hide(hide_advanced_sound);

	const bool hide_advanced = tab_ != ADVANCED_TAB;
	advanced_.hide(hide_advanced);
	advanced_button_.hide(hide_advanced);
}

}

void show_preferences_dialog(display& disp, const config& game_cfg)
{
	std::vector<std::string> items;

	std::string const pre = IMAGE_PREFIX + std::string("icons/icon-");
	char const sep = COLUMN_SEPARATOR;
	items.push_back(pre + "general.png" + sep + sgettext("Prefs section^General"));
	items.push_back(pre + "display.png" + sep + sgettext("Prefs section^Display"));
	items.push_back(pre + "music.png" + sep + sgettext("Prefs section^Sound"));
	items.push_back(pre + "advanced.png" + sep + sgettext("Advanced section^Advanced"));

	preferences_dialog dialog(disp,game_cfg);
	std::vector<gui::preview_pane*> panes;
	panes.push_back(&dialog);

	gui::show_dialog2(disp,NULL,_("Preferences"),"",gui::CLOSE_ONLY,&items,&panes);
	return;
}

}
