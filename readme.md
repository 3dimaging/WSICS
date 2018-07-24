# Introduction #

# Usage #

The Stain Normalization algorithm can accept whole slide images in a tiled image format, or as a flat patch. The image reading is provided by the ASAP project (reference), and thus provides any format that it does as well.

## Input and Output ##

In order to execute the algorithm, the **input** parameter can be set with a file or directory path. If a directory path is offered, the algorithm will attempt to normalize all readable files within that directory. The output commands are interpreted based on the input, if a file is inserted, the output paths will be considered file paths as well, and vice versa for directories.
...
-i, --input [file or directory path]
...

If a directory is used as an input origin, the filenames themselves will be used to output the resulting normalized whole-slide images. To customize this output, a **prefix** or **postfix** can be set through the corresponding parameters.
...
--prefix [prefix string]
--postfix [postfix string]
...

To output normalized images, the image_output parameter must be set. This also outputs the LUT table as a tif file. If only the LUT table is required, the lut_output parameter can be set. 
...
--image_output [file or directory path]
--lut_output [file or directory path]
...

A whole-slide image can be used as a reference for the normalization, which allows the algorithm to transform the source image to closer resemble the reference used. To do this, a set of template parameters are created and exported into a CSV file. This template can be generated by setting the template_output parameter, and used by setting the template_input parameter.
...
--template_input [file path to a template CSV file]
--template_output [file path to an output location]
...

If one or multiple whole-slide images contain ink or other heavily represented artifacts, then the **ink** or **k** parameters can be set. This will reduce the chance of a patch being selected to collect training pixels for the algorithm, and thus potentially insuring a better result.
...
-k, --ink
...

## Training ##

The creation of the Look Up Table utilizes a Naïve Bayes classifier to determine the probabilities of a pixel belonging to a certain class. In order to train this classifier, pixels corresponding to the background, Eosine and Hematoxyline colored tissue is selected and added to a training set. The max_training and min_training parameters define the total size of the training set created and the minimum amount of selected pixels required to continue an execution.
...
--max_training [size as integer]
--min_training [size as integer]
...

The training pixels are selected from tiles that contain little to no background, this is done by calculating the amount of pixels that are near white or black. If this is higher than the percentage indicated by the background_threshold parameter, then the tile isn’t utilized for the selection of training pixels.
...
--background_threshold [float value between 0 and 1]
...

The selection of Hematoxylin colored pixels is done by detecting ellipses within the tissue and then calculating the mean red density value of the HSD color space. The hema_percentile parameter then defines which ellipse mean is selected to serve as threshold for the selection of Hematoxylin pixels.
...
--hema_percentile [float value between 0 and 1]
...

The Eosin threshold is defined by collecting all pixels that aren’t considered as background or Hematoxylin stained, and then selecting the one that is nearest within the list, based on the percentage defined by the eosin_percentile parameter. The pixels passing the threshold are then utilized for the creation and collection of Eosin stained pixels.
...
--eosin_percentile [float value between 0 and 1]
...