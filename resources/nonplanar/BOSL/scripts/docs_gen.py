#!/usr/bin/env python

from __future__ import print_function

import os
import re
import sys
import math
import random
import os.path
import argparse
import subprocess



def get_header_link(name):
    refpat = re.compile("[^a-z0-9_ -]")
    return refpat.sub("", name.lower()).replace(" ", "-")


def toc_entry(name, indent, count=None):
    lname = "{0}{1}".format(
        ("%d. " % count) if count else "",
        name
    )
    ref = get_header_link(lname)
    if name.endswith( (")", "}", "]") ):
        name = "`" + name.replace("\\", "") + "`"
    return "{0}{1} [{2}](#{3})".format(
        indent,
        ("%d." % count) if count else "-",
        name,
        ref
    )


def mkdn_esc(txt):
    out = ""
    quotpat = re.compile(r'([^`]*)(`[^`]*`)(.*$)');
    while txt:
        m = quotpat.match(txt)
        if m:
            out += m.group(1).replace(r'_', r'\_')
            out += m.group(2)
            txt = m.group(3)
        else:
            out += txt.replace(r'_', r'\_')
            txt = ""
    return out


def get_comment_block(lines, prefix, blanks=1):
    out = []
    blankcnt = 0
    while lines:
        if not lines[0].startswith(prefix + " "):
            break
        line = lines.pop(0).rstrip().lstrip("/")
        if line == "":
            blankcnt += 1
            if blankcnt >= blanks:
                break
        else:
            blankcnt = 0
            line = line[len(prefix):]
        out.append(line)
    return (lines, out)


class ImageProcessing(object):
    def __init__(self):
        self.examples = []
        self.commoncode = []
        self.imgroot = ""
        self.keep_scripts = False

    def set_keep_scripts(self, x):
        self.keep_scripts = x

    def add_image(self, libfile, imgfile, code, extype):
        self.examples.append((libfile, imgfile, code, extype))

    def set_commoncode(self, code):
        self.commoncode = code

    def process_examples(self, imgroot):
        self.imgroot = imgroot
        for libfile, imgfile, code, extype in self.examples:
            self.gen_example_image(libfile, imgfile, code, extype)

    def gen_example_image(self, libfile, imgfile, code, extype):
        OPENSCAD = "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD"
        CONVERT = "/usr/local/bin/convert"
        COMPARE = "/usr/local/bin/compare"

        if extype == "NORENDER":
            return

        scriptfile = "tmp_{0}.scad".format(imgfile.replace(".", "_"))

        stdlibs = ["constants.scad", "math.scad", "transforms.scad", "shapes.scad", "debug.scad"]
        script = ""
        for lib in stdlibs:
            script += "include <BOSL/%s>\n" % lib
        if libfile not in stdlibs:
            script += "include <BOSL/%s>\n" % libfile
        for line in self.commoncode:
            script += line+"\n"
        for line in code:
            script += line+"\n"

        with open(scriptfile, "w") as f:
            f.write(script)

        if "Med" in extype:
            imgsizes = ["800,600", "400x300"]
        elif "Big" in extype:
            imgsizes = ["1280,960", "640x480"]
        elif "distribute" in script:
            print(script)
            imgsizes = ["800,600", "400x300"]
        else:  # Small
            imgsizes = ["480,360", "240x180"]

        print("")
        print("{}: {}".format(libfile, imgfile))

        tmpimgs = []
        if "Spin" in extype:
            for ang in range(0,359,10):
                tmpimgfile = "{0}tmp_{2}_{1}.png".format(self.imgroot, ang, imgfile.replace(".", "_"))
                arad = ang * math.pi / 180;
                eye = "{0},{1},{2}".format(
                    500*math.cos(arad),
                    500*math.sin(arad),
                    500 if "Flat" in extype else 500*math.sin(arad)
                )
                scadcmd = [
                    OPENSCAD,
                    "-o", tmpimgfile,
                    "--imgsize={}".format(imgsizes[0]),
                    "--hardwarnings",
                    "--projection=o",
                    "--view=axes,scales",
                    "--autocenter",
                    "--viewall",
                    "--camera", eye+",0,0,0"
                ]
                if "FR" in extype:  # Force render
                    scadcmd.extend(["--render", ""])
                scadcmd.append(scriptfile)
                print(" ".join(scadcmd))
                res = subprocess.call(scadcmd)
                if res != 0:
                    print(script)
                    sys.exit(res)
                tmpimgs.append(tmpimgfile)
        else:
            tmpimgfile = self.imgroot + "tmp_" + imgfile
            scadcmd = [
                OPENSCAD,
                "-o", tmpimgfile,
                "--imgsize={}".format(imgsizes[0]),
                "--hardwarnings",
                "--projection=o",
                "--view=axes,scales",
                "--autocenter",
                "--viewall"
            ]
            if "2D" in extype:  # 2D viewpoint
                scadcmd.extend(["--camera", "0,0,0,0,0,0,500"])
            if "FR" in extype:  # Force render
                scadcmd.extend(["--render", ""])
            scadcmd.append(scriptfile)

            print(" ".join(scadcmd))
            res = subprocess.call(scadcmd)
            if res != 0:
                print(script)
                sys.exit(res)
            tmpimgs.append(tmpimgfile)

        if not self.keep_scripts:
            os.unlink(scriptfile)
        targimgfile = self.imgroot + imgfile
        newimgfile = self.imgroot + "_new_" + imgfile
        if len(tmpimgs) == 1:
            cnvcmd = [CONVERT, tmpimgfile, "-resize", imgsizes[1], newimgfile]
            print(" ".join(cnvcmd))
            res = subprocess.call(cnvcmd)
            if res != 0:
                sys.exit(res)
            os.unlink(tmpimgs.pop(0))
        else:
            cnvcmd = [
                CONVERT,
                "-delay", "25",
                "-loop", "0",
                "-coalesce",
                "-scale", imgsizes[1],
                "-fuzz", "2%",
                "+dither",
                "-layers", "Optimize",
                "+map"
            ]
            cnvcmd.extend(tmpimgs)
            cnvcmd.append(newimgfile)
            print(" ".join(cnvcmd))
            res = subprocess.call(cnvcmd)
            if res != 0:
                sys.exit(res)
            for tmpimg in tmpimgs:
                os.unlink(tmpimg)

        # Time to compare image.
        if not os.path.isfile(targimgfile):
            print("NEW IMAGE installed.")
            os.rename(newimgfile, targimgfile)
        else:
            if targimgfile.endswith(".gif"):
                cmpcmd = ["cmp", newimgfile, targimgfile]
                print(" ".join(cmpcmd))
                res = subprocess.call(cmpcmd)
                issame = res == 0
            else:
                cmpcmd = [COMPARE, "-metric", "MAE", newimgfile, targimgfile, "null:"]
                print(" ".join(cmpcmd))
                p = subprocess.Popen(cmpcmd, shell=False, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, close_fds=True)
                issame = p.stdout.read().strip() == "0 (0)"
            if issame:
                print("Image unchanged.")
                os.unlink(newimgfile)
            else:
                print("Image UPDATED.")
                os.unlink(targimgfile)
                os.rename(newimgfile, targimgfile)


imgprc = ImageProcessing()


class LeafNode(object):
    def __init__(self):
        self.name = ""
        self.leaftype = ""
        self.status = ""
        self.description = []
        self.usages = []
        self.arguments = []
        self.side_effects = []
        self.examples = []

    @classmethod
    def match_line(cls, line, prefix):
        if line.startswith(prefix + "Constant: "):
            return True
        if line.startswith(prefix + "Function: "):
            return True
        if line.startswith(prefix + "Module: "):
            return True
        return False

    def add_example(self, title, code, extype):
        self.examples.append((title, code, extype))

    def parse_lines(self, lines, prefix):
        blankcnt = 0
        expat = re.compile(r"^(Examples?)(\(([^\)]*)\))?: *(.*)$")
        while lines:
            if prefix and not lines[0].startswith(prefix.strip()):
                break
            line = lines.pop(0).rstrip()
            if line.lstrip("/").strip() == "":
                blankcnt += 1
                if blankcnt >= 2:
                    break
                continue
            blankcnt = 0
            line = line[len(prefix):]
            if line.startswith("Constant:"):
                leaftype, title = line.split(":", 1)
                self.name = title.strip()
                self.leaftype = leaftype.strip()
            if line.startswith("Function:"):
                leaftype, title = line.split(":", 1)
                self.name = title.strip()
                self.leaftype = leaftype.strip()
            if line.startswith("Module:"):
                leaftype, title = line.split(":", 1)
                self.name = title.strip()
                self.leaftype = leaftype.strip()
            if line.startswith("Status:"):
                dummy, status = line.split(":", 1)
                self.status = status.strip()
            if line.startswith("Description:"):
                dummy, desc = line.split(":", 1)
                desc = desc.strip()
                if desc:
                    self.description.append(desc)
                lines, block = get_comment_block(lines, prefix)
                self.description.extend(block)
            if line.startswith("Usage:"):
                dummy, title = line.split(":", 1)
                title = title.strip()
                lines, block = get_comment_block(lines, prefix)
                self.usages.append([title, block])
            if line.startswith("Arguments:"):
                lines, block = get_comment_block(lines, prefix)
                for line in block:
                    if "=" not in line:
                        print("Error: bad argument line:")
                        print(line)
                        sys.exit(2)
                    argname, argdesc = line.split("=", 1)
                    argname = argname.strip()
                    argdesc = argdesc.strip()
                    self.arguments.append([argname, argdesc])
            if line.startswith("Side Effects:"):
                lines, block = get_comment_block(lines, prefix)
                self.side_effects.extend(block)
            m = expat.match(line)
            if m:  # Example(TYPE):
                plural = m.group(1) == "Examples"
                extype = m.group(3)
                title = m.group(4)
                lines, block = get_comment_block(lines, prefix)
                if not extype:
                    extype = "3D" if self.leaftype == "Module" else "NORENDER"
                if not plural:
                    self.add_example(title=title, code=block, extype=extype)
                else:
                    for line in block:
                        self.add_example(title="", code=[line], extype=extype)
        return lines

    def gen_md(self, fileroot, imgroot):
        out = []
        if self.name:
            out.append("### " + mkdn_esc(self.name))
            out.append("")
        if self.status:
            out.append("**{0}**".format(mkdn_esc(self.status)))
            out.append("")
        for title, usages in self.usages:
            if not title:
                title = "Usage"
            out.append("**{0}**:".format(mkdn_esc(title)))
            for usage in usages:
                out.append("- {0}".format(mkdn_esc(usage)))
            out.append("")
        if self.description:
            out.append("**Description**:")
            for line in self.description:
                out.append(mkdn_esc(line))
            out.append("")
        if self.arguments:
            out.append("Argument        | What it does")
            out.append("--------------- | ------------------------------")
            for argname, argdesc in self.arguments:
                argname = argname.replace(" / ", "` / `")
                out.append(
                    "{0:15s} | {1}".format(
                        "`{0}`".format(argname),
                        mkdn_esc(argdesc)
                    )
                )
            out.append("")
        if self.side_effects:
            out.append("**Side Effects**:")
            for sfx in self.side_effects:
                out.append("- " + mkdn_esc(sfx))
            out.append("")
        exnum = 0
        for title, excode, extype in self.examples:
            exnum += 1
            if len(self.examples) < 2:
                extitle = "**Example**:"
            else:
                extitle = "**Example {0}**:".format(exnum)
            if title:
                extitle += " " + mkdn_esc(title)
            out.append(extitle)
            out.append("")
            for line in excode:
                out.append("    " + line)
            out.append("")
            san_name = re.sub(r"[^A-Za-z0-9_]", "", self.name)
            imgfile = "{0}{1}.{2}".format(
                san_name,
                ("_%d" % exnum) if exnum > 1 else "",
                "gif" if "Spin" in extype else "png"
            )
            if extype != "NORENDER":
                out.append(
                    "![{0} Example{1}]({2}{3})".format(
                        mkdn_esc(self.name),
                        (" %d" % exnum) if len(self.examples) > 1 else "",
                        imgroot,
                        imgfile
                    )
                )
                out.append("")
                imgprc.add_image(fileroot+".scad", imgfile, excode, extype)
        out.append("---")
        out.append("")
        return out


class Section(object):
    fignum = 0
    def __init__(self):
        self.name = ""
        self.description = []
        self.leaf_nodes = []
        self.figures = []

    @classmethod
    def match_line(cls, line, prefix):
        if line.startswith(prefix + "Section: "):
            return True
        return False

    def add_figure(self, figtitle, figcode, figtype):
        self.figures.append((figtitle, figcode, figtype))

    def parse_lines(self, lines, prefix):
        line = lines.pop(0).rstrip()
        dummy, title = line.split(": ", 1)
        self.name = title.strip()
        lines, block = get_comment_block(lines, prefix, blanks=2)
        self.description.extend(block)
        blankcnt = 0
        figpat = re.compile(r"^(Figures?)(\(([^\)]*)\))?: *(.*)$")
        while lines:
            if prefix and not lines[0].startswith(prefix.strip()):
                break
            line = lines.pop(0).rstrip()
            if line.lstrip("/").strip() == "":
                blankcnt += 1
                if blankcnt >= 2:
                    break
                continue
            blankcnt = 0
            line = line[len(prefix):]
            m = figpat.match(line)
            if m:  # Figures(TYPE):
                plural = m.group(1) == "Figures"
                figtype = m.group(3)
                title = m.group(4)
                lines, block = get_comment_block(lines, prefix)
                if not figtype:
                    figtype = "3D" if self.figtype == "Module" else "NORENDER"
                if not plural:
                    self.add_figure(title, block, figtype)
                else:
                    for line in block:
                        self.add_figure("", [line], figtype)
        return lines

    def gen_md_toc(self, count):
        indent=""
        out = []
        if self.name:
            out.append(toc_entry(self.name, indent, count=count))
            indent += "    "
        for node in self.leaf_nodes:
            out.append(toc_entry(node.name, indent))
        out.append("")
        return out

    def gen_md(self, count, fileroot, imgroot):
        out = []
        if self.name:
            out.append("# %d. %s" % (count, mkdn_esc(self.name)))
            out.append("")
        if self.description:
            in_block = False
            for line in self.description:
                if line.startswith("```"):
                    in_block = not in_block
                if in_block or line.startswith("    "):
                    out.append(line)
                else:
                    out.append(mkdn_esc(line))
            out.append("")
        for title, figcode, figtype in self.figures:
            Section.fignum += 1
            figtitle = "**Figure {0}**:".format(Section.fignum)
            if title:
                figtitle += " " + mkdn_esc(title)
            out.append(figtitle)
            out.append("")
            imgfile = "{}{}.{}".format(
                "figure",
                Section.fignum,
                "gif" if "Spin" in figtype else "png"
            )
            if figtype != "NORENDER":
                out.append(
                    "![{0} Figure {1}]({2}{3})".format(
                        mkdn_esc(self.name),
                        Section.fignum,
                        imgroot,
                        imgfile
                    )
                )
                out.append("")
                imgprc.add_image(fileroot+".scad", imgfile, figcode, figtype)
        in_block = False
        for node in self.leaf_nodes:
            out += node.gen_md(fileroot, imgroot)
        return out


class LibFile(object):
    def __init__(self):
        self.name = ""
        self.description = []
        self.commoncode = []
        self.sections = []
        self.dep_sect = None

    def parse_lines(self, lines, prefix):
        currsect = None
        constpat = re.compile(r"^([A-Z_0-9][A-Z_0-9]*) *=.*  // (.*$)")
        while lines:
            while lines and prefix and not lines[0].startswith(prefix.strip()):
                line = lines.pop(0)
                m = constpat.match(line)
                if m:
                    if currsect == None:
                        currsect = Section()
                        self.sections.append(currsect)
                    node = LeafNode();
                    node.extype = "Constant"
                    node.name = m.group(1).strip()
                    node.description.append(m.group(2).strip())
                    currsect.leaf_nodes.append(node)

            # Check for LibFile header.
            if lines and lines[0].startswith(prefix + "LibFile: "):
                line = lines.pop(0).rstrip()
                dummy, title = line.split(": ", 1)
                self.name = title.strip()
                lines, block = get_comment_block(lines, prefix, blanks=2)
                self.description.extend(block)

            # Check for CommonCode header.
            if lines and lines[0].startswith(prefix + "CommonCode:"):
                lines.pop(0)
                lines, block = get_comment_block(lines, prefix)
                self.commoncode.extend(block)

            # Check for Section header.
            if lines and Section.match_line(lines[0], prefix):
                sect = Section()
                lines = sect.parse_lines(lines, prefix)
                self.sections.append(sect)
                currsect = sect

            # Check for LeafNode.
            if lines and LeafNode.match_line(lines[0], prefix):
                node = LeafNode()
                lines = node.parse_lines(lines, prefix)
                deprecated = node.status.startswith("DEPRECATED")
                if deprecated:
                    if self.dep_sect == None:
                        self.dep_sect = Section()
                        self.dep_sect.name = "Deprecations"
                    sect = self.dep_sect
                else:
                    if currsect == None:
                        currsect = Section()
                        self.sections.append(currsect)
                    sect = currsect
                sect.leaf_nodes.append(node)
            if lines:
                lines.pop(0)
        return lines

    def gen_md(self, fileroot, imgroot):
        imgprc.set_commoncode(self.commoncode)
        out = []
        if self.name:
            out.append("# Library File " + mkdn_esc(self.name))
            out.append("")
        if self.description:
            in_block = False
            for line in self.description:
                if line.startswith("```"):
                    in_block = not in_block
                if in_block or line.startswith("    "):
                    out.append(line)
                else:
                    out.append(mkdn_esc(line))
            out.append("")
            in_block = False
        if self.name or self.description:
            out.append("---")
            out.append("")

        if self.sections or self.dep_sect:
            out.append("# Table of Contents")
            out.append("")
            cnt = 0
            for sect in self.sections:
                cnt += 1
                out += sect.gen_md_toc(cnt)
            if self.dep_sect:
                cnt += 1
                out += self.dep_sect.gen_md_toc(cnt)
            out.append("---")
            out.append("")

        cnt = 0
        for sect in self.sections:
            cnt += 1
            out += sect.gen_md(cnt, fileroot, imgroot)
        if self.dep_sect:
            cnt += 1
            out += self.dep_sect.gen_md(cnt, fileroot, imgroot)
        return out


def processFile(infile, outfile=None, gen_imgs=False, imgroot="", prefix=""):
    if imgroot and not imgroot.endswith('/'):
        imgroot += "/"

    libfile = LibFile()
    with open(infile, "r") as f:
        lines = f.readlines()
        libfile.parse_lines(lines, prefix)

    if outfile == None:
        f = sys.stdout
    else:
        f = open(outfile, "w")

    fileroot = os.path.splitext(os.path.basename(infile))[0]
    outdata = libfile.gen_md(fileroot, imgroot)
    for line in outdata:
        print(line, file=f)

    if gen_imgs:
        imgprc.process_examples(imgroot)

    if outfile:
        f.close()


def main():
    parser = argparse.ArgumentParser(prog='docs_gen')
    parser.add_argument('-k', '--keep-scripts', action="store_true",
                        help="If given, don't delete the temporary image OpenSCAD scripts.")
    parser.add_argument('-c', '--comments-only', action="store_true",
                        help='If given, only process lines that start with // comments.')
    parser.add_argument('-i', '--images', action="store_true",
                        help='If given, generate images for examples with OpenSCAD.')
    parser.add_argument('-I', '--imgroot', default="",
                        help='The directory to put generated images in.')
    parser.add_argument('-o', '--outfile',
                        help='Output file, if different from infile.')
    parser.add_argument('infile', help='Input filename.')
    args = parser.parse_args()

    imgprc.set_keep_scripts(args.keep_scripts)
    processFile(
        args.infile,
        outfile=args.outfile,
        gen_imgs=args.images,
        imgroot=args.imgroot,
        prefix="// " if args.comments_only else ""
    )

    sys.exit(0)


if __name__ == "__main__":
    main()


# vim: expandtab tabstop=4 shiftwidth=4 softtabstop=4 nowrap
