# Cryonic Engine

Cryonic Engine is an open source C++ game engine designed to facilitate game development with a focus on simplicity, flexibility, and modular architecture.

![Cryonic Engine Screenshot](https://ezengine.net/pages/samples/media/showcase-1.jpg)

## Design Philosophy

Cryonic Engine is built with modularity at its core, enabling developers to use all available functionality or selectively integrate individual features. The engine emphasizes:

- **Simplicity:** Clean, easy-to-understand architecture accessible to developers of all skill levels
- **Flexibility:** Modular design allowing easy customization and extension
- **Solid Foundation:** Efficient core systems that provide value even when using custom rendering solutions
- **Rapid Prototyping:** Feature-rich editor supporting both visual scripting and custom C++ code

Larger features are implemented through engine and editor plugins, making them easy to remove or replace. Systems like sound (Fmod), physics (Jolt), and particle effects are all provided through this plugin architecture.

## Supported Platforms

The full engine functionality is currently available on Windows with Direct3D 11 rendering. Work on porting the renderer to Vulkan is ongoing to expand platform support.

All non-rendering related functionality compiles on Windows, Android, Linux, and MacOS. See the [platform documentation](http://ezEngine.net/pages/docs/build/supported-platforms.html) for details.

**Current Build Support:**
- Windows
- Linux
- macOS
- Android
- Web

## Getting Started

### Building the Engine (Windows)

Open a Windows Terminal to clone and build the engine:

1. `git clone https://github.com/ElectroGamesDev/Cryonic-Engine.git`
2. `cd Cryonic-Engine`
3. `.\GenerateWin64vs2022.bat`
4. Open the Visual Studio solution `Workspace\vs2022x64\ezEngine_vs2022x64.sln` and build everything
5. Launch the `Editor` project from Visual Studio and open one of the sample projects

For other platforms and detailed build instructions, visit the [documentation](https://ezengine.net/pages/docs/build/building-ez.html).

### Prebuilt Binaries

See the [releases](https://github.com/CryonicEngine/CryonicEngine/releases) page for recent changes and prebuilt binaries.

## Key Capabilities

- **2D and 3D Rendering:** Powerful rendering capabilities for immersive game experiences
- **GameObject-Component System:** Intuitive entity organization and behavior management
- **Visual Scripting:** Node visual scripting is supported with Event Sheets in development
- **Physics Integration:** Full 2D and 3D physics support
- **Advanced Animation:** Complete 2D and 3D animation system
- **Audio Playback:** Integrated sound system via Fmod
- **Comprehensive Input:** Keyboard, mouse, and gamepad support
- **Scene Management:** Robust scene loading and saving system
- **Asset Management:** Efficient asset pipeline with hot reloading capabilities
- **Visual Editor:** Feature-rich editor with advanced GUI editing tools
- **Lighting & Shadows:** Complete lighting system with shadow mapping support

## Documentation and Resources

- **High-level Documentation:** [Cryonic Engine Documentation](https://ezengine.net/pages/docs/docs-overview.html)
- **API Reference:** [Code API Documentation](https://ezengine.github.io/api-docs/)
- **Editor Overview:** [Feature-rich Editor Guide](http://ezengine.net/pages/getting-started/editor-overview.html)
- **Samples:** [Available Sample Projects](https://ezengine.net/pages/samples/samples-overview.html)
- **Screenshots:** [Gallery](https://ezengine.net/pages/samples/screenshots.html)
- **Tutorial Videos:** [YouTube Channel](https://www.youtube.com/@ezEngine)
- **FAQ:** [Frequently Asked Questions](https://ezengine.net/pages/getting-started/faq.html)

## Contributing

Cryonic Engine welcomes contributions from the community. Whether you're fixing bugs, adding features, or improving documentation, your help is appreciated.

**How to Contribute:**
- Fork the repository and create a feature branch
- Make your enhancements with clear, documented code
- Submit a pull request with a detailed description of your changes
- For guidelines and best practices, see the [contribution guide](https://ezengine.net/pages/getting-started/how-to-contribute.html)

**Feedback:**
- Bug reports and feature requests: Open an [issue](https://github.com/CryonicEngine/CryonicEngine/issues)
- Questions and discussions: [Discord](https://discord.gg/Yqw52WDtW6) or [support@cryonicengine.com](mailto:support@cryonicengine.com).

## License Summary
[Click here for the full license](https://github.com/CryonicEngine/CryonicEngine/blob/main/LICENSE.md)

### Key Points

- **Usage:** You can use, modify, and distribute the Engine, Modifications, or Derived Engines for any purpose, including commercial projects.  
- **Sublicensing:** Allowed only under the same license terms.  

---

### Splash Screen

- **Every Product must display the Cryonic Engine Splash Screen for at startup.**  
- Removal is only allowed with a **paid plan** that removes the slash screen. Unauthorized removal is a breach.

---

### Royalties

- **Royalty Rate:** 4% on all revenue **above $250,000 USD per Product that uses the engine**.  
- Applies to **any Product using the Engine or Derived Engine**, modified or unmodified.  

---

### ⚖️ Legal Notes

- **Warranty:** Engine is provided "AS IS", without any warranty.  
- **Liability:** Licensor is not liable for indirect or consequential damages.  

---

By using the Engine, you agree to the full Cryonic Engine License.
