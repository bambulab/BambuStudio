# Formatting Comments for Docs

Documentation and example images are generated automatically from source code comments by the `scripts/docs_gen.py` script.  Not all comments are added to the wiki.  Just those comment blocks starting with certain keywords:

- `// LibFile: NAME`
- `// Section: NAME`
- `// Module: NAME`
- `// Function: NAME`
- `// Constant: NAME`

## LibFile:

LibFile blocks can be followed by multiple lines that can be added as markdown text after the header. Indentation is important, as it denotes the end of block.

```
// LibFile: foo.scad
//   You can have several lines of markdown formatted text here.
//   You just need to make sure that each line is indented, with
//   at least three spaces after the comment marker.  You can
//   denote a paragraph break with a comment line with three
//   trailing spaces.
//   
//   The end of the block is denoted by a line without a comment.
```

## Section:

Section blocks can be followed by multiple lines that can be added as markdown text after the header. Indentation is important, as it denotes the end of block.

Sections can also include Figures; images generated from code that is not shown in a code block.

```
// Section: Foobar
//   You can have several lines of markdown formatted text here.
//   You just need to make sure that each line is indented with
//   at least three spaces after the comment marker.  You can
//   denote a paragraph break with a comment line with three
//   trailing spaces.
//   
//   The end of the block is denoted by a line without a comment,
//   or a line that is unindented after the comment.
// Figure: Figure description
//   cylinder(h=100, d1=75, d2=50);
//   up(100) cylinder(h=100, d1=50, d2=75);
// Figure(Spin): Animated figure that spins to show all faces.
//   cube([10,100,50], center=true);
//   cube([100,10,30], center=true);
```

## CommonCode:

CommonCode blocks can be used to denote code that can be shared between all of the Figure and Example blocks in the file, without being shown itself.  Indentation is important.  Less than three spaces indent denotes the end of the block

```
// CommonCode:
//   module text3d(text, h=0.01, size=3) {
//       linear_extrude(height=h, convexity=10) {
//           text(text=text, size=size, valign="center", halign="center");
//       }
//   }
```

## Module:/Function:/Constant:

Module, Function, and Constant docs blocks all have a similar specific format.  Most sub-blocks are optional, except the Module/Function/Constant line, and the Description block.

Valid sub-blocks are:

- `Status: DEPRECATED, use blah instead.` - Optional, used to denote deprecation.
- `Usage: Optional Usage Title` - Optional.  Multiple allowed.  Followed by an indented block of usage patterns.  Optional arguments should be in braces like `[opt]`.  Alternate args should be separated by a vertical bar like `r|d`. 
- `Description:` - Can be single-line or a multi-line block of the description.
- `Arguments:` - Denotes start of an indented block of argument descriptions.  Each line has the argument name, a space, an equals, another space, then the description for the argument all on one line. Like `arg = The argument description`.  If you really need to explain an argument in longer form, explain it in the Description.
- `Side Effects:` - Denotes the start of a block describing the side effects, such as `$special_var`s that are set.
- `Example:` - Denotes the beginning of a multi-line example code block.
- `Examples:` - Denotes the beginning of a block of examples, where each line will be shows as a separate example with a separate image if needed.

Modules blocks will generate images for each example block. Function and Constant blocks will only generate images for example blocks if they have `2D` or `3D` tags.  Example blocks can have tags added by putting then inside parentheses before the colon.  Ie: `Examples(BigFlatSpin):`.  

The full set of optional example tags are:

- `2D`: Orient camera in a top-down view for showing 2D objects.
- `3D`: Orient camera in an oblique view for showing 3D objects. Used to force an Example sub-block to generate an image in Function and Constant blocks.
- `Spin`: Animate camera orbit around the `[0,1,1]` axis to display all sides of an object.
- `FlatSpin` : Animate camera orbit around the Z axis, above the XY plane.
- `FR`: Force full rendering from OpenSCAD, instead of the normal preview.
- `Small`: Make the image small sized.
- `Med`: Make the image medium sized.
- `Big`: Make the image big sized.

Indentation is important, as it denotes the end of sub-block.

```
// Module: foo()
// Status: DEPRECATED, use BLAH instead.
// Usage: Optional Usage Description
//   foo(foo, bar, [qux]);
//   foo(bar, baz, [qux]);
// Usage: Another Optional Usage Description
//   foo(foo, flee, flie, [qux])
// Description: Short description.
// Description:
//   A longer, multi-line description.
//   All description blocks are added together.
//   You _can_ use *markdown* notation as well.
//   You can end multi-line blocks by un-indenting the
//   next line, or by using a blank comment line like this:
//
// Arguments:
//   foo = This is the description of the foo argument.  All on one line.
//   bar = This is the description of the bar argument.  All on one line.
//   baz = This is the description of the baz argument.  All on one line.
//   qux = This is the description of the qux argument.  All on one line.
//   flee = This is the description of the flee argument.  All on one line.
//   flie = This is the description of the flie argument.  All on one line.
// Side Effects:
//   `$floo` gets set to the floo value.
// Examples: Each line below gets its own example block and image.
//   foo(foo="a", bar="b");
//   foo(foo="b", baz="c");
// Example: Multi-line example.
//   lst = [
//       "multi-line examples",
//       "are shown in one block",
//       "with a single image.",
//   ];
//   foo(lst, 23, "blah");
// Example(2D): Example to show as 2D top-down rendering.
//   foo(foo="b", baz="c", qux=true);
// Example(Spin): Example orbiting the [0,1,1] axis.
//   foo(foo="b", baz="c", qux="full");
// Example(FlatSpin): Example orbiting the Z axis from above.
//   foo(foo="b", baz="c", qux="full2");
```



