#!/bin/bash
rm -fr ./clean-icons
npx svgo -f ./icons -o ./cleaned-icons
npx svgtofont --sources ./cleaned-icons --output ./fonts --fontName icons
