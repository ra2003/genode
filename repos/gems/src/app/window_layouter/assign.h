/*
 * \brief  Assignment of window-manager clients to target area
 * \author Norman Feske
 * \date   2018-09-27
 */

/*
 * Copyright (C) 2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _ASSIGN_H_
#define _ASSIGN_H_

/* Genode includes */
#include <util/list_model.h>
#include <base/registry.h>
#include <os/buffered_xml.h>

/* local includes */
#include <types.h>
#include <target.h>
#include <decorator_margins.h>

namespace Window_layouter { class Assign; }


class Window_layouter::Assign : public List_model<Assign>::Element
{
	public:

		/*
		 * Used for associating windows with assignments. Hosted in 'Window'
		 * objects.
		 */
		struct Member : Registry<Member>::Element
		{
			Window &window;

			Member(Registry<Member> &registry, Window &window)
			: Registry<Member>::Element(registry, *this), window(window) { }
		};

		typedef String<80> Label;

	private:

		Registry<Member> _members { };

		Label const _label;
		Label const _label_prefix;
		Label const _label_suffix;

		Target::Name _target_name { };

		bool _pos_defined  = false;
		bool _xpos_any     = false;
		bool _ypos_any     = false;
		bool _size_defined = false;
		bool _maximized    = false;

		Point _pos  { };
		Area  _size { };

	public:

		Assign(Xml_node assign)
		:
			_label       (assign.attribute_value("label",        Label())),
			_label_prefix(assign.attribute_value("label_prefix", Label())),
			_label_suffix(assign.attribute_value("label_suffix", Label()))
		{ }

		void update(Xml_node assign)
		{
			_target_name  = assign.attribute_value("target", Target::Name());
			_pos_defined  = assign.has_attribute("xpos")  && assign.has_attribute("ypos");
			_size_defined = assign.has_attribute("width") && assign.has_attribute("height");
			_maximized    = assign.attribute_value("maximized", false);
			_xpos_any     = assign.attribute_value("xpos", String<20>()) == "any";
			_ypos_any     = assign.attribute_value("ypos", String<20>()) == "any";
			_pos          = point_attribute(assign);
			_size         = area_attribute(assign);
		}

		/*
		 * Used by 'Assign_list::update_from_xml'
		 */
		bool matches(Xml_node node) const
		{
			return node.attribute_value("label",        Label()) == _label
			    && node.attribute_value("label_prefix", Label()) == _label_prefix
			    && node.attribute_value("label_suffix", Label()) == _label_suffix;
		}

		/**
		 * Calculate window geometry
		 */
		Rect window_geometry(unsigned win_id, Area client_size, Rect target_geometry,
		                     Decorator_margins const &decorator_margins) const
		{
			bool const floating = !_maximized && _pos_defined;

			if (!floating)
				return target_geometry;

			/*
			 * Position floating window
			 *
			 * In contrast to a tiled or maximized window that is hard
			 * constrained by the size of the target geometry even if the
			 * client requests a larger size, floating windows respect the size
			 * defined by the client.
			 *
			 * This way, while floating, applications are able to resize their
			 * window according to the application's state. For example, a
			 * bottom-right resize corner of a Qt application is handled by the
			 * application, not the window manager, but it should still work as
			 * expected by the user. Note that the ability of the application
			 * to influence the layout is restricted to its window size. The
			 * window position cannot be influenced. This limits the ability of
			 * an application to impersonate other applications.
			 */
			Point const any_pos(150*win_id % 800, 30 + (100*win_id % 500));

			Point const pos(_xpos_any ? any_pos.x() : _pos.x(),
			                _ypos_any ? any_pos.y() : _pos.y());

			Rect const inner(pos, client_size);
			Rect const outer = decorator_margins.outer_geometry(inner);

			return Rect(outer.p1() + target_geometry.p1(), outer.area());
		}

		/**
		 * Call 'fn' with 'Registry<Member>' if label matches assignment
		 *
		 * This method is used for associating assignments to windows.
		 */
		template <typename FN>
		void with_matching_members_registry(Label const &label, FN const &fn)
		{
			bool const label_matches = (_label.valid() && label == _label);

			bool const prefix_matches =
				_label_prefix.valid() &&
				!strcmp(label.string(),
				        _label_prefix.string(), _label_prefix.length() - 1);

			bool suffix_matches = false;
			if (label.length() >= _label_suffix.length()) {
				unsigned const offset = label.length() - _label_suffix.length();
				suffix_matches = !strcmp(_label.string() + offset, _label_suffix.string());
			}

			bool const wildcard_matches = !_label.valid()
			                           && (!_label_prefix.valid() || prefix_matches)
			                           && (!_label_suffix.valid() || suffix_matches);

			if (label_matches || wildcard_matches)
				fn(_members);
		}

		Target::Name target_name() const { return _target_name; }

		/**
		 * Used to generate <assign> nodes of windows captured via wildcard
		 */
		template <typename FN>
		void for_each_wildcard_member(FN const &fn) const
		{
			/* skip non-wildcards */
			if (_label.valid())
				return;

			_members.for_each([&] (Assign::Member const &member) { fn(member); });
		}

		/**
		 * Used to bring wildcard-matching windows to front
		 */
		template <typename FN>
		void for_each_wildcard_member(FN const &fn)
		{
			if (_label.valid())
				return;

			_members.for_each([&] (Assign::Member &member) { fn(member); });
		}

		bool floating() const { return _pos_defined; }

		bool wildcard() const { return !_label.valid(); }

		/**
		 * Generate <assign> node
		 */
		void gen_assign_attr(Xml_generator &xml) const
		{
			if (_label.valid())        xml.attribute("label",        _label);
			if (_label_prefix.valid()) xml.attribute("label_prefix", _label_prefix);
			if (_label_suffix.valid()) xml.attribute("label_suffix", _label_suffix);

			xml.attribute("target", _target_name);
		}

		void gen_geometry_attr(Xml_generator &xml) const
		{
			if (_pos_defined) {
				if (_xpos_any) xml.attribute("xpos", "any");
				else           xml.attribute("xpos", _pos.x());

				if (_ypos_any) xml.attribute("ypos", "any");
				else           xml.attribute("ypos", _pos.y());
			}

			if (_size_defined) {
				xml.attribute("width",  _size.w());
				xml.attribute("height", _size.h());
			}

			if (_maximized)
				xml.attribute("maximized", true);
		}

		template <typename FN>
		void for_each_member(FN const &fn) { _members.for_each(fn); }

		template <typename FN>
		void for_each_member(FN const &fn) const { _members.for_each(fn); }
};

#endif /* _ASSIGN_H_ */
