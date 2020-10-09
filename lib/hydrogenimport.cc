/*
 * liquidsfz - sfz sampler
 *
 * Copyright (C) 2020  Stefan Westerfeld
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include "hydrogenimport.hh"
#include "pugixml.hh"
#include "log.hh"

#include <cmath>

#include <vector>

namespace LiquidSFZInternal {

using pugi::xml_document;
using pugi::xml_node;

using std::vector;
using std::string;

struct Region
{
  string sample;
  int lovel = 0;
  int hivel = 0;
};

static void
cleanup_regions (vector<Region>& regions)
{
  vector<Region *> note_region (128);

  /* first step: find and assign exactly one region for each midi note */
  for (int note = 1; note <= 127; note++)
    {
      for (auto& r : regions)
        {
          if (r.lovel <= note && r.hivel >= note)
            {
              /* simple case: note is within the region range */
              note_region[note] = &r;
              break;
            }
        }
      if (note_region[note] == nullptr) /* not assigned yet */
        {
          /* try all regions, assign note to region with closest lovel */
          int best_dist = 128;
          for (auto& r : regions)
            {
              int dist = abs (note - r.lovel);
              if (dist < best_dist)
                {
                  best_dist = dist;
                  note_region[note] = &r;
                }
            }
        }
    }
  /* generate new lovel / hivel from matching notes */
  for (auto& r : regions)
    {
      int lovel = 128, hivel = 0;

      for (int note = 1; note <= 127; note++)
        {
          if (note_region[note] == &r)
            {
              if (note < lovel)
                lovel = note;
              if (note > hivel)
                hivel = note;
            }
        }
      r.lovel = lovel;
      r.hivel = hivel;
    }
}

static double
xml_to_double (const xml_node& node, double def)
{
  string str = node.text().as_string();
  return (str != "") ? string_to_double (str) : def;
}

bool
HydrogenImport::detect (const string& filename)
{
  xml_document doc;
  auto result = doc.load_file (filename.c_str());
  if (!result)
    return false;

  xml_node drumkit_info = doc.child ("drumkit_info");
  xml_node instrument_list = drumkit_info.child ("instrumentList");
  for (xml_node instrument : instrument_list.children ("instrument"))
    {
      xml_node name = instrument.child ("name");
      if (name)
        return true;
    }
  return false;
}

bool
HydrogenImport::parse (const string& filename, string& out)
{
  constexpr bool use_midi_out_note = false;

  xml_document doc;
  auto result = doc.load_file (filename.c_str());
  if (!result)
    {
      printf ("load error\n");
      return false;
    }
  xml_node drumkit_info = doc.child ("drumkit_info");
  xml_node instrument_list = drumkit_info.child ("instrumentList");
  int instrument_index = 0;
  int region_count = 0;
  int group = 1;
  for (xml_node instrument : instrument_list.children ("instrument"))
    {
      xml_node name = instrument.child ("name");
      out += string_printf ("// %s\n", name.text().as_string());
      int key = instrument_index + 36;

      double volume = xml_to_double (instrument.child ("volume"), 1);

      int midi_out_note = instrument.child ("midiOutNote").text().as_int();
      if (use_midi_out_note && midi_out_note > 0)
        key = midi_out_note;

      out += string_printf ("<group>\n");
      out += string_printf ("  key=%d\n", key);
      out += string_printf ("  loop_mode=one_shot\n");
      out += string_printf ("  amp_velcurve_1=0.008\n");
      out += string_printf ("  group=%d\n", group);
      out += string_printf ("  off_by=%d\n", group);
      out += string_printf ("  volume=%f\n", db_from_factor (volume, -144));
      out += string_printf ("\n");

      vector<Region> regions;
      auto do_layer = [&region_count,&regions] (const xml_node& layer)
        {
          xml_node filename = layer.child ("filename");
          string sample = filename.text().as_string();

          int lovel = lrint (xml_to_double (layer.child ("min"), 0) * 127);
          int hivel = lrint (xml_to_double (layer.child ("max"), 1) * 127);
          regions.push_back ({ sample, lovel, hivel });
        };
      // new style: layer is in an instrumentComponent node
      xml_node instrument_component = instrument.child ("instrumentComponent");
      for (xml_node layer : instrument_component.children ("layer"))
        do_layer (layer);

      // old style: layer is a child of the instrument
      for (xml_node layer : instrument.children ("layer"))
        do_layer (layer);

      cleanup_regions (regions);

      for (auto r : regions)
        {
          out += string_printf ("  <region>\n");
          out += string_printf ("    lovel=%d hivel=%d\n", r.lovel, r.hivel);
          out += string_printf ("    sample=%s\n", r.sample.c_str());
          out += string_printf ("\n");

          region_count++;
        }

      // even older style: filename is child of this node
      xml_node filename = instrument.child ("filename");
      if (filename)
        {
          string sample = filename.text().as_string();

          out += string_printf ("  <region>\n");
          out += string_printf ("    sample=%s\n", sample.c_str());
          out += string_printf ("\n");

          region_count++;
        }
      out += string_printf ("\n");

      group++;
      instrument_index++;
    }

  if (region_count == 0)
    {
      printf ("load error: no hydrogen regions found in input file\n");
      return false;
    }
  return true;
}

}