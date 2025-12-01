# KiCad Project - Split Flap Display PCB

This directory contains the KiCad project files converted from EasyEDA source files.

## Files

- `SplitFlap.kicad_pro` - Main KiCad project file
- `SplitFlap.kicad_sch` - Schematic file
- `SplitFlap.kicad_pcb` - PCB layout file
- `SplitFlap.kicad_sym` - Symbol library
- `EasyEDA.pretty/` - Footprint library directory

## Opening in KiCad

1. **Install KiCad 6.x or later** (required for these files)
   - Download from: https://www.kicad.org/download/

2. **Open the project:**
   - Launch KiCad
   - File → Open Project
   - Navigate to this directory and open `SplitFlap.kicad_pro`

3. **Add libraries (if needed):**
   - The footprint library `EasyEDA.pretty` should be automatically detected
   - If not, go to Preferences → Manage Footprint Libraries → Project Specific Libraries
   - Add the `EasyEDA.pretty` directory

## Assigning 3D Models to Components

To get a complete 3D STEP file with all components, you need to assign 3D models to each footprint. The components are already placed on the PCB, but their 3D models need to be linked.

### Method 1: Using KiCad's 3D Viewer (Recommended)

1. **Open the PCB Editor:**
   - In KiCad, click on the PCB Editor icon or open `SplitFlap.kicad_pcb`

2. **View in 3D:**
   - Click the **3D Viewer** button (or View → 3D Viewer)
   - This shows which components have 3D models and which don't

3. **Assign 3D Models to Footprints:**
   - Right-click on a component footprint → **Properties**
   - Click the **3D Models** tab
   - Click **Add 3D Model**
   - Browse to find the 3D model file (`.step`, `.wrl`, or `.obj` format)
   - Adjust position/rotation if needed
   - Click **OK**

4. **Download 3D Models:**
   - **GrabCAD**: https://grabcad.com/library (search by part number)
   - **3D ContentCentral**: https://www.3dcontentcentral.com/
   - **SnapEDA**: https://www.snapeda.com/ (free account required)
   - **KiCad 3D Model Library**: Check if models exist in KiCad's default library
   - **Component Manufacturers**: Many provide STEP files on their websites

### Method 2: Batch Assign Using Footprint Editor

1. **Open Footprint Editor:**
   - Tools → Footprint Editor

2. **Edit Each Footprint:**
   - File → Open Footprint
   - Navigate to `EasyEDA.pretty/` and open the footprint
   - Click the **3D Models** tab
   - Add the 3D model file
   - Save the footprint

3. **Update PCB:**
   - The changes will automatically apply to all instances of that footprint on the PCB

### Method 3: Using EasyEDA Component Library (if available)

The converter may have created references to 3D models. Check if they exist:
- Look in the footprint files in `EasyEDA.pretty/` for 3D model references
- The converter creates references like: `${KICAD6_3DMODEL_DIR}/EasyEDA.3dshapes/footprint_name.wrl`
- You may need to download these models from EasyEDA/LCSC or find equivalent models

## Exporting 3D STEP File

Once 3D models are assigned:

1. **Open the PCB Editor:**
   - In KiCad, click on the PCB Editor icon or open `SplitFlap.kicad_pcb`

2. **Verify 3D Models:**
   - Open 3D Viewer (View → 3D Viewer)
   - Check that components show 3D models (not just flat pads)
   - If components appear flat, they need 3D models assigned

3. **Export STEP file:**
   - Go to **File → Export → STEP...**
   - Choose a location and filename (e.g., `SplitFlap_PCB.step`)
   - **Important**: Check the options:
     - ✅ Include 3D models for components
     - ✅ Include board outline
   - Click **Save**

4. **The STEP file will include:**
   - PCB board outline
   - All component footprints positioned correctly
   - 3D models of components (if assigned)

## Key Components on This PCB

Based on the BOM, main components include:
- **ULN2003A** (U4) - DIP-16 package - Common 3D models available
- **L7805CV** (U3) - TO-220 package - Standard regulator, models widely available
- **AMS1117-3.3** (U9) - SOT-223 package - Common LDO, models available
- **Arduino Nano** (U1) - Custom footprint - May need custom model or approximate
- **ESP-01S** (U2) - Custom WiFi module - May need custom model
- **JST Connectors** (U6, U7, U8, U10) - XH series - Models available from JST
- **DIP Switch** (SW1) - Custom footprint - May need approximate model
- **Passive components** (C0805, R0805, etc.) - Standard SMD - Models widely available

### Quick Tips for Finding 3D Models:

1. **Standard packages** (0805, SOT-23, TO-220, etc.):
   - Search by package name on GrabCAD or 3D ContentCentral
   - KiCad's default library may have some

2. **Specific parts** (ULN2003A, L7805CV):
   - Search by part number on manufacturer websites
   - Many have STEP files in their product pages

3. **Custom parts** (Arduino Nano, ESP-01S):
   - Arduino: Official Arduino website or GrabCAD
   - ESP-01S: Ai-Thinker website or approximate with similar ESP module models

4. **Connectors** (JST XH series):
   - JST provides 3D models on their website
   - Search "JST XH 3D model STEP"

## Notes

- The conversion created 3 messages during PCB conversion - check the User.Cmts layer in the PCB editor for details
- Some manual adjustments may be needed for component 3D models
- Components are already placed on the PCB from the EasyEDA conversion
- You only need to assign 3D models - the physical placement is already correct
- The original EasyEDA source files are in `../EasyEDA_Source/`

## Original Source Files

- PCB: `../EasyEDA_Source/PCB_PCB_Splitflap_2022-06-04.json`
- Schematic: `../EasyEDA_Source/SCH_SplitFlap_2022-06-04.json`
- BOM: `../BOM_PCB_Splitflap_2022-03-11.csv`

