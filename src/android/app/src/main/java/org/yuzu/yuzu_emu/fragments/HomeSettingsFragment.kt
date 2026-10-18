// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

package org.yuzu.yuzu_emu.fragments

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.content.pm.PackageManager
import android.os.Bundle
import android.provider.DocumentsContract
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import android.widget.TextView
import android.widget.LinearLayout
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.documentfile.provider.DocumentFile
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.transition.MaterialSharedAxis
import org.yuzu.yuzu_emu.HomeNavigationDirections
import org.yuzu.yuzu_emu.NativeLibrary
import org.yuzu.yuzu_emu.R
import org.yuzu.yuzu_emu.YuzuApplication
import org.yuzu.yuzu_emu.adapters.HomeSettingAdapter
import org.yuzu.yuzu_emu.databinding.FragmentHomeSettingsBinding
import org.yuzu.yuzu_emu.features.DocumentProvider
import org.yuzu.yuzu_emu.features.fetcher.SpacingItemDecoration
import org.yuzu.yuzu_emu.features.settings.model.Settings
import org.yuzu.yuzu_emu.features.settings.ui.SettingsSubscreen
import org.yuzu.yuzu_emu.model.DriverViewModel
import org.yuzu.yuzu_emu.model.HomeSetting
import org.yuzu.yuzu_emu.model.HomeViewModel
import org.yuzu.yuzu_emu.ui.main.MainActivity
import org.yuzu.yuzu_emu.utils.FileUtil
import org.yuzu.yuzu_emu.utils.GpuDriverHelper
import org.yuzu.yuzu_emu.utils.S25UltraProfile
import org.yuzu.yuzu_emu.utils.Log
import org.yuzu.yuzu_emu.utils.LosslessScalingHelper
import org.yuzu.yuzu_emu.utils.ViewUtils.updateMargins

class HomeSettingsFragment : Fragment() {
    private var _binding: FragmentHomeSettingsBinding? = null
    private val binding get() = _binding!!

    private lateinit var mainActivity: MainActivity

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val driverViewModel: DriverViewModel by activityViewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        reenterTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentHomeSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        homeViewModel.setStatusBarShadeVisibility(visible = false)
        mainActivity = requireActivity() as MainActivity
        binding.toolbarHomeSettings.setNavigationOnClickListener {
            findNavController().popBackStack()
        }
        binding.toolbarHomeSettings.title = getString(R.string.preferences_settings)

        val optionsList: MutableList<HomeSetting> = mutableListOf<HomeSetting>().apply {
            add(
                HomeSetting(
                    R.string.advanced_settings,
                    R.string.settings_description,
                    R.drawable.ic_settings,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_ROOT
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.app_settings,
                    R.string.app_settings_description,
                    R.drawable.ic_palette,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_APP_SETTINGS
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.preferences_controls,
                    R.string.preferences_controls_description,
                    R.drawable.ic_controller,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_INPUT
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            // Galaxy-specific tools stay hidden on other Android devices.
            if (S25UltraProfile.isSupportedDevice()) {
                add(
                    HomeSetting(
                        R.string.s25_profile_title,
                        R.string.s25_profile_description,
                        R.drawable.ic_graphics,
                        { S25UltraProfile.showPresetDialog(requireContext()) }
                    )
                )
                add(
                    HomeSetting(
                        R.string.s25_diagnostics_title,
                        R.string.s25_diagnostics_description,
                        R.drawable.ic_system,
                        { S25UltraProfile.showDiagnosticsDialog(requireContext()) }
                    )
                )
                add(
                    HomeSetting(
                        R.string.s25_converter_title,
                        R.string.s25_converter_description,
                        R.drawable.ic_install,
                        { startActivity(Intent().setClassName(requireContext(), "org.yuzu.yuzu_emu.activities.S25UltraConverterActivity")) }
                    )
                )
                add(
                    HomeSetting(
                        R.string.s25_library_scan_title,
                        R.string.s25_library_scan_description,
                        R.drawable.ic_folder_open,
                        {
                            startActivity(
                                Intent().setClassName(
                                    requireContext(),
                                    "org.yuzu.yuzu_emu.activities.S25LibraryDiagnosticsActivity"
                                )
                            )
                        }
                    )
                )
                add(
                    HomeSetting(
                        R.string.s25_online_library_title,
                        R.string.s25_online_library_description,
                        R.drawable.ic_folder_open,
                        {
                            startActivity(
                                Intent().setClassName(
                                    requireContext(),
                                    "org.yuzu.yuzu_emu.activities.S25OnlineLibraryActivity"
                                )
                            )
                        }
                    )
                )
            }
            add(
                HomeSetting(
                    R.string.profile_manager,
                    R.string.profile_manager_description,
                    R.drawable.ic_account_circle,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.PROFILE_MANAGER,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.gpu_driver_manager,
                    R.string.install_gpu_driver_description,
                    R.drawable.ic_build,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.DRIVER_MANAGER,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    },
                    { true },
                    R.string.custom_driver_not_supported,
                    R.string.custom_driver_not_supported_description,
                    driverViewModel.selectedDriverTitle
                )
            )
            if (GpuDriverHelper.isAdrenoGpu()) {
                add(
                    HomeSetting(
                        R.string.freedreno_settings_title,
                        R.string.gpu_driver_settings,
                        R.drawable.ic_graphics,
                        {
                            val action =
                                HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                                    SettingsSubscreen.FREEDRENO_SETTINGS,
                                    null
                                )
                            binding.root.findNavController().navigate(action)
                        }
                    )
                )
            }
            add(
                HomeSetting(
                    R.string.post_processing,
                    R.string.post_processing_description,
                    R.drawable.ic_post_processing,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsActivity(
                            null,
                            Settings.MenuTag.SECTION_POST_PROCESSING
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.lossless_scaling,
                    R.string.lossless_scaling_description,
                    R.drawable.ic_duck,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.LOSSLESS_MANAGER,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    },
                    { true },
                    0,
                    0,
                    LosslessScalingHelper.statusText
                )
            )
            add(
                HomeSetting(
                    R.string.multiplayer,
                    R.string.multiplayer_description,
                    R.drawable.ic_two_users,
                    {
                        mainActivity.displayMultiplayerDialog()
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.applets,
                    R.string.applets_description,
                    R.drawable.ic_applet,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.APPLET_LAUNCHER,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    },
                    { NativeLibrary.isFirmwareAvailable() },
                    R.string.applets_error_firmware,
                    R.string.applets_error_description
                )
            )
            add(
                HomeSetting(
                    R.string.manage_yuzu_data,
                    R.string.manage_yuzu_data_description,
                    R.drawable.ic_install,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.INSTALLABLE,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.manage_game_folders,
                    R.string.select_games_folder_description,
                    R.drawable.ic_add,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.GAME_FOLDERS,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.verify_installed_content,
                    R.string.verify_installed_content_description,
                    R.drawable.ic_check_circle,
                    {
                        ProgressDialogFragment.newInstance(
                            requireActivity(),
                            titleId = R.string.verifying,
                            cancellable = true
                        ) { progressCallback, _ ->
                            val result = NativeLibrary.verifyInstalledContents(progressCallback)
                            return@newInstance if (progressCallback.invoke(100, 100)) {
                                // Invoke the progress callback to check if the process was cancelled
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_no_result,
                                    descriptionId = R.string.verify_no_result_description
                                )
                            } else if (result.isEmpty()) {
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_success,
                                    descriptionId = R.string.operation_completed_successfully
                                )
                            } else {
                                val failedNames = result.joinToString("\n")
                                val errorMessage = YuzuApplication.appContext.getString(
                                    R.string.verification_failed_for,
                                    failedNames
                                )
                                MessageDialogFragment.newInstance(
                                    titleId = R.string.verify_failure,
                                    descriptionString = errorMessage
                                )
                            }
                        }.show(parentFragmentManager, ProgressDialogFragment.TAG)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.share_log,
                    R.string.share_log_description,
                    R.drawable.ic_log,
                    { shareLog() }
                )
            )
            add(
                HomeSetting(
                    R.string.share_gpu_log,
                    R.string.share_gpu_log_description,
                    R.drawable.ic_log,
                    { shareGpuLog() }
                )
            )
            add(
                HomeSetting(
                    R.string.open_user_folder,
                    R.string.open_user_folder_description,
                    R.drawable.ic_folder_open,
                    { openFileManager() }
                )
            )
            add(
                HomeSetting(
                    R.string.system_information,
                    R.string.system_information_description,
                    R.drawable.ic_system,
                    {
                        SystemInfoDialogFragment.newInstance()
                            .show(parentFragmentManager, SystemInfoDialogFragment.TAG)
                    }
                )
            )
            add(
                HomeSetting(
                    R.string.about,
                    R.string.about_description,
                    R.drawable.ic_info_outline,
                    {
                        val action = HomeNavigationDirections.actionGlobalSettingsSubscreenActivity(
                            SettingsSubscreen.ABOUT,
                            null
                        )
                        binding.root.findNavController().navigate(action)
                    }
                )
            )
        }

        val optionsAdapter = HomeSettingAdapter(
            requireActivity() as AppCompatActivity,
            viewLifecycleOwner,
            optionsList
        )
        binding.homeSettingsList.apply {
            layoutManager =
                GridLayoutManager(requireContext(), resources.getInteger(R.integer.grid_columns))
            adapter = optionsAdapter
            val spacing = resources.getDimensionPixelSize(R.dimen.spacing_small)
            addItemDecoration(SpacingItemDecoration(spacing))
        }

        // ASTRYX navigation-only grouping. This never modifies emulation state.
        // Every original HomeSetting is in exactly one category, preserving actions.
        if (requireContext().packageName.startsWith("dev.eden.eden_emulator.s25ultra")) {
            setupAstryxNavigation(optionsList, optionsAdapter)
        }

        setInsets()
    }

    private fun setupAstryxNavigation(
        options: List<HomeSetting>,
        adapter: HomeSettingAdapter
    ) {
        val mainIds = setOf(
            R.string.advanced_settings,
            R.string.app_settings,
            R.string.preferences_controls,
            R.string.s25_profile_title,
            R.string.gpu_driver_manager,
            R.string.manage_game_folders
        )
        val toolIds = setOf(
            R.string.s25_converter_title,
            R.string.s25_library_scan_title,
            R.string.s25_online_library_title,
            R.string.profile_manager,
            R.string.multiplayer,
            R.string.applets,
            R.string.manage_yuzu_data,
            R.string.verify_installed_content,
            R.string.open_user_folder
        )
        val tabs = listOf(
            getString(R.string.astryx_settings_tab_main),
            getString(R.string.astryx_settings_tab_tools),
            getString(R.string.astryx_settings_tab_advanced)
        )
        val palette = listOf(0xFF33CAEF.toInt(), 0xFF111B2B.toInt())
        val preferences =
            requireContext().getSharedPreferences("astryx_settings_navigation", Context.MODE_PRIVATE)
        val group = MaterialButtonToggleGroup(requireContext()).apply {
            isSingleSelection = true
            isSelectionRequired = true
            orientation = LinearLayout.HORIZONTAL
        }
        val buttons = tabs.map { label ->
            MaterialButton(
                requireContext(), null,
                com.google.android.material.R.attr.materialButtonOutlinedStyle
            ).apply {
                id = View.generateViewId()
                text = label
                isAllCaps = false
                textSize = 12f
                minHeight = 0
                minimumHeight = 0
                isCheckable = true
                setPadding(2, 8, 2, 8)
            }.also { button ->
                group.addView(
                    button,
                    LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f)
                )
            }
        }
        val header = TextView(requireContext()).apply {
            textSize = 12f
            letterSpacing = 0.06f
            setTextColor(0xFFA6B9CE.toInt())
            setPadding(8, 14, 8, 14)
        }
        binding.linearLayoutSettings.addView(group, 0)
        binding.linearLayoutSettings.addView(header, 1)

        fun refresh(index: Int) {
            val selected = index.coerceIn(0, 2)
            val visible = options.filter { item ->
                when (selected) {
                    0 -> item.titleId in mainIds
                    1 -> item.titleId in toolIds
                    else -> item.titleId !in mainIds && item.titleId !in toolIds
                }
            }
            // No deletion and no configuration writes; only view filtering.
            adapter.replaceList(visible)
            header.text = when (selected) {
                0 -> getString(R.string.astryx_settings_main_description)
                1 -> getString(R.string.astryx_settings_tools_description)
                else -> getString(R.string.astryx_settings_advanced_description)
            }
            buttons.forEachIndexed { i, button ->
                val selectedButton = i == selected
                button.backgroundTintList = ColorStateList.valueOf(
                    palette[if (selectedButton) 0 else 1]
                )
                button.setTextColor(if (selectedButton) Color.BLACK else Color.WHITE)
            }
            preferences.edit().putInt("tab", selected).apply()
        }
        group.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked) refresh(buttons.indexOfFirst { it.id == checkedId })
        }
        val selected = preferences.getInt("tab", 0).coerceIn(0, 2)
        group.check(buttons[selected].id)
    }

    override fun onStart() {
        super.onStart()
        exitTransition = null
    }

    override fun onResume() {
        super.onResume()
        driverViewModel.updateDriverNameForGame(null)
        LosslessScalingHelper.refreshStatus()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun openFileManager() {
        // First, try to open the user data folder directly
        try {
            startActivity(getFileManagerIntentOnDocumentProvider(Intent.ACTION_VIEW))
            return
        } catch (_: ActivityNotFoundException) {
        }

        try {
            startActivity(getFileManagerIntentOnDocumentProvider("android.provider.action.BROWSE"))
            return
        } catch (_: ActivityNotFoundException) {
        }

        // Just try to open the file manager, try the package name used on "normal" phones
        try {
            startActivity(getFileManagerIntent("com.google.android.documentsui"))
            showNoLinkNotification()
            return
        } catch (_: ActivityNotFoundException) {
        }

        try {
            // Next, try the AOSP package name
            startActivity(getFileManagerIntent("com.android.documentsui"))
            showNoLinkNotification()
            return
        } catch (_: ActivityNotFoundException) {
        }

        Toast.makeText(
            requireContext(),
            resources.getString(R.string.no_file_manager),
            Toast.LENGTH_LONG
        ).show()
    }

    private fun getFileManagerIntent(packageName: String): Intent {
        // Fragile, but some phones don't expose the system file manager in any better way
        val intent = Intent(Intent.ACTION_MAIN)
        intent.setClassName(packageName, "com.android.documentsui.files.FilesActivity")
        intent.flags = Intent.FLAG_ACTIVITY_NEW_TASK
        return intent
    }

    private fun getFileManagerIntentOnDocumentProvider(action: String): Intent {
        val authority = "${requireContext().packageName}.user"
        val intent = Intent(action)
        intent.addCategory(Intent.CATEGORY_DEFAULT)
        intent.data = DocumentsContract.buildRootUri(authority, DocumentProvider.ROOT_ID)
        intent.addFlags(
            Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION or
                Intent.FLAG_GRANT_PREFIX_URI_PERMISSION or
                Intent.FLAG_GRANT_WRITE_URI_PERMISSION
        )
        return intent
    }

    private fun showNoLinkNotification() {
        val builder = NotificationCompat.Builder(
            requireContext(),
            getString(R.string.notice_notification_channel_id)
        )
            .setSmallIcon(R.drawable.ic_stat_notification_logo)
            .setContentTitle(getString(R.string.notification_no_directory_link))
            .setContentText(getString(R.string.notification_no_directory_link_description))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
        // TODO: Make the click action for this notification lead to a help article

        with(NotificationManagerCompat.from(requireContext())) {
            if (ActivityCompat.checkSelfPermission(
                    requireContext(),
                    Manifest.permission.POST_NOTIFICATIONS
                ) != PackageManager.PERMISSION_GRANTED
            ) {
                Toast.makeText(
                    requireContext(),
                    resources.getString(R.string.notification_permission_not_granted),
                    Toast.LENGTH_LONG
                ).show()
                return
            }
            notify(0, builder.build())
        }
    }

    // Share the current log if we just returned from a game but share the old log
    // if we just started the app and the old log exists.
    private fun shareLog() {
        val currentLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/eden_log.txt"
            )
        )!!
        val oldLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/eden_log.txt.old.txt"
            )
        )!!

        val intent = Intent(Intent.ACTION_SEND)
            .setDataAndType(currentLog.uri, FileUtil.TEXT_PLAIN)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        if (!Log.gameLaunched && oldLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, oldLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else if (currentLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, currentLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_log)))
        } else {
            Toast.makeText(
                requireContext(),
                getText(R.string.share_log_missing),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun shareGpuLog() {
        val currentLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/eden_gpu.log"
            )
        )!!
        val oldLog = DocumentFile.fromSingleUri(
            mainActivity,
            DocumentsContract.buildDocumentUri(
                DocumentProvider.AUTHORITY,
                "${DocumentProvider.ROOT_ID}/log/eden_gpu.log.old.txt"
            )
        )!!

        val intent = Intent(Intent.ACTION_SEND)
            .setDataAndType(currentLog.uri, FileUtil.TEXT_PLAIN)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        if (!Log.gameLaunched && oldLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, oldLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_gpu_log)))
        } else if (currentLog.exists()) {
            intent.putExtra(Intent.EXTRA_STREAM, currentLog.uri)
            startActivity(Intent.createChooser(intent, getText(R.string.share_gpu_log)))
        } else {
            Toast.makeText(
                requireContext(),
                getText(R.string.share_gpu_log_missing),
                Toast.LENGTH_SHORT
            ).show()
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { _, windowInsets ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            binding.appbarHomeSettings.updateMargins(
                left = barInsets.left + cutoutInsets.left,
                right = barInsets.right + cutoutInsets.right
            )

            binding.scrollViewSettings.updatePadding(
                bottom = barInsets.bottom
            )

            binding.homeSettingsList.updatePadding(
                left = barInsets.left + cutoutInsets.left,
                right = barInsets.right + cutoutInsets.right
            )

            windowInsets
        }
}
